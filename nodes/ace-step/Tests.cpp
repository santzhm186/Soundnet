#include "Kernel.hpp"
#include "Node.hpp"
#include "../../platform/musicgen/MusicGenBackend.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

static int failures=0;
static void check(bool ok,const char* what) {
    if(!ok) {std::printf("FAIL: %s\n",what);++failures;}
}

static std::string readText(const std::string& path) {
    std::ifstream in(path);
    if(!in)return {};
    return std::string((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
}

// A deterministic one-second ramp at 48 kHz so playback is easy to assert.
// Frame 0 is non-zero: a region that starts at offset 0 must still be audible.
static std::shared_ptr<const tri::AudioSample> rampSample(int frames=48000) {
    auto sample=std::make_shared<tri::AudioSample>();
    sample->rate=48000;sample->channels=2;
    sample->samples.resize(size_t(frames)*2);
    for(int i=0;i<frames;++i) {
        const float v=float(i+1)/float(frames+1);
        sample->samples[size_t(i)*2]=v;
        sample->samples[size_t(i)*2+1]=v;
    }
    return sample;
}

static void runPlayback(tri::NodeKernel& kernel,tri::KernelContext& context) {
    kernel.process(context);
}

int main() {
    // ---- descriptor -------------------------------------------------------
    {
        const auto& module=tri::nodes::ace_step::module();
        const tri::NodeSpec& spec=module.spec;
        check(std::strcmp(spec.key,"soundnet.ace-step")==0,"ace-step key");
        check(spec.capabilities&tri::CapTask,"ace-step declares a task");
        check(spec.assetKind&&std::strcmp(spec.assetKind,"audio")==0,"ace-step embeds audio");
        check(!spec.legacyAdapter,"ace-step runs on the public kernel path");
        check(spec.inputs.size()==1&&spec.inputs[0].signal==tri::Signal::Trigger,"ace-step trigger input");
        check(spec.outputs.size()==1&&spec.outputs[0].signal==tri::Signal::Audio,"ace-step audio output");
        check(spec.kernelFactory!=nullptr,"ace-step has a kernel factory");
        check(module.parameters.size()==8,"ace-step parameter count");
        bool takes=false;
        for(const auto& text:spec.texts)if(std::strcmp(text.id,"gen.takes")==0)takes=text.hidden;
        check(!takes,"take history is stored as platform resources, not text paths");
        tri::KernelConfig good;
        good.parameters[tri::nodes::ace_step::startID]=0.1f;
        good.parameters[tri::nodes::ace_step::endID]=0.9f;
        std::string why;
        check(tri::nodes::ace_step::validate(good,why),"valid region passes");
        good.parameters[tri::nodes::ace_step::endID]=0.05f;
        check(!tri::nodes::ace_step::validate(good,why),"inverted region is rejected");
    }

    // ---- playback kernel --------------------------------------------------
    {
        auto kernel=std::unique_ptr<tri::NodeKernel>(tri::nodes::ace_step::create());
        kernel->prepare(48000,256);
        tri::KernelConfig config;
        auto sample=rampSample(48000);
        config.audio=sample;
        config.parameters[tri::nodes::ace_step::gainID]=1.f;
        config.parameters[tri::nodes::ace_step::fadeID]=0.f;   // hard edges for assertions
        kernel->configure(config);

        float outL[256]{},outR[256]{};
        tri::KernelOutput output;output.id="audio";output.left=outL;output.right=outR;
        tri::KernelContext context;context.frames=256;context.sampleRate=48000;
        context.outputs=&output;context.outputCount=1;

        // No trigger, no sample content yet: silence.
        runPlayback(*kernel,context);
        check(outL[0]==0&&outL[255]==0,"silent without a trigger");

        // Trigger start at frame 10: the ramp starts there.
        tri::KernelTriggerEvent events[2]{{10,true},{0,false}};
        tri::KernelInput trigger;trigger.id="trigger";trigger.triggers=events;trigger.triggerCount=1;
        context.inputs=&trigger;context.inputCount=1;
        runPlayback(*kernel,context);
        check(outL[0]==0&&outL[10]>0,"playback begins at the trigger frame");
        check(outL[11]>outL[10]||outL[11]==1.f,"ramp progresses after start");
        // Position persists across blocks without new events.
        context.inputs=nullptr;context.inputCount=0;
        runPlayback(*kernel,context);
        check(outL[255]>outL[0],"position continues across blocks");

        // Trigger stop with fade 0 silences immediately; reset clears state.
        tri::KernelTriggerEvent stop{0,false};
        trigger.triggers=&stop;trigger.triggerCount=1;
        context.inputs=&trigger;context.inputCount=1;
        kernel->reset(tri::KernelResetReason::Stop);
        runPlayback(*kernel,context);
        check(outL[255]==0,"stop resets playback");

        // Region bounds: end 0.0005 of a 48000-frame sample = 24 frames.
        config.parameters[tri::nodes::ace_step::endID]=0.0005f;
        kernel->configure(config);
        events[0]={0,true};trigger.triggers=events;trigger.triggerCount=1;
        runPlayback(*kernel,context);
        bool tail=true;
        for(int i=30;i<256;++i)if(outL[i]!=0)tail=false;
        check(tail,"playback stops at the region end");

        // Loop wraps inside the region.
        config.parameters[tri::nodes::ace_step::loopID]=1.f;
        kernel->configure(config);
        events[0]={0,true};
        runPlayback(*kernel,context);
        check(outL[250]>0||outL[255]>0,"loop keeps producing audio past the region end");

        // Bypass silences and clears state.
        context.bypassed=true;
        runPlayback(*kernel,context);
        check(outL[255]==0,"bypass silences the node");
        context.bypassed=false;

        // Empty sample: silence, no crash.
        tri::KernelConfig empty;
        kernel->configure(empty);
        runPlayback(*kernel,context);
        check(outL[255]==0,"missing asset stays silent");
    }

    // ---- backend contract -------------------------------------------------
    const std::string dir="/tmp/ace-step-node-tests";
    ::mkdir(dir.c_str(),0755);
    const std::string wav=dir+"/stub.wav";
    {
        std::ofstream out(wav,std::ios::binary);
        out<<"RIFF----WAVEfmt ";  // content irrelevant; only existence matters
    }
    // A stub that records its arguments and copies the wav into place.
    const std::string script=dir+"/stub.sh";
    {
        std::ofstream stub(script);
        stub<<"#!/bin/sh\nprintf '%s\\n' \"$@\" > '"+dir+"/args.txt'\n"
            <<"out=\"\"\nwhile [ $# -gt 0 ]; do\n  if [ \"$1\" = \"--out\" ]; then out=\"$2\"; shift 2; else shift; fi\ndone\n"
            <<"cp '"+wav+"' \"$out\"\nexit 0\n";
    }
    ::chmod(script.c_str(),0755);

    auto waitFor=[](tri::musicgen::AudioCppCliBackend& backend,uint64_t task,
                    tri::musicgen::GenResult& out,std::string& why) {
        for(int i=0;i<200;++i) {
            const auto status=backend.poll(task,out,why);
            if(status!=tri::musicgen::GenTaskStatus::Running)return status;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return tri::musicgen::GenTaskStatus::Failed;
    };

    // Argument contract: prompt goes to --text, duration is explicit and the
    // yue2-only session/request options are never used.
    {
        const std::string work=dir+"/w-defaults";::mkdir(work.c_str(),0755);
        tri::musicgen::AudioCppCliBackend backend;
        tri::musicgen::GenRequest r;
        r.family="ace_step";
        r.execPath=script;r.modelPath=dir;r.workDir=work;
        r.prompt="warm lo-fi hip hop with a soft rhodes piano";
        r.seconds=17;r.steps=4;r.seed=7;r.lyrics="la la la";
        uint64_t task=0;std::string why;
        check(backend.submit(r,task,why),"ace-step request submits");
        tri::musicgen::GenResult out;
        check(waitFor(backend,task,out,why)==tri::musicgen::GenTaskStatus::Succeeded,"ace-step stub succeeds");
        const std::string arguments=readText(dir+"/args.txt");
        check(arguments.find("--text\nwarm lo-fi hip hop with a soft rhodes piano")!=std::string::npos,
              "the prompt reaches the CLI through --text");
        check(arguments.find("--duration-seconds\n17")!=std::string::npos,"duration is explicit");
        check(arguments.find("--num-inference-steps\n4")!=std::string::npos,"steps reach the CLI");
        check(arguments.find("--lyrics\nla la la")!=std::string::npos,"lyrics reach the CLI");
        check(arguments.find("style=")==std::string::npos,"ace-step never uses the yue2 style option");
        check(arguments.find("yue2.model_gguf=")==std::string::npos,"ace-step never pins a yue2 GGUF");
        check(arguments.find("cot=")==std::string::npos,"ace-step has no symbolic planner option");
        check(arguments.find("stop_after")==std::string::npos,"ace-step never stops after a score");
        check(!out.wavPath.empty(),"audio path is reported");
    }
    // Score-only and external scores are yue2-only concepts, rejected up front.
    {
        const std::string work=dir+"/w-scoreonly";::mkdir(work.c_str(),0755);
        tri::musicgen::AudioCppCliBackend backend;
        tri::musicgen::GenRequest r;
        r.family="ace_step";r.execPath=script;r.modelPath=dir;r.workDir=work;
        r.outMode=1;
        uint64_t task=0;std::string why;
        check(!backend.submit(r,task,why),"score-only ace-step request is rejected");
        check(why.find("仅乐谱")!=std::string::npos,"the reason names the mode");
        tri::musicgen::GenRequest fed=r;
        fed.outMode=0;fed.abcText="X:1\nK:C\n";
        check(!backend.submit(fed,task,why),"an external score is rejected");
        check(why.find("外部乐谱")!=std::string::npos,"the reason names the score");
    }
    // Cancellation kills the child and reports Cancelled.
    {
        const std::string work=dir+"/w-cancel";::mkdir(work.c_str(),0755);
        const std::string slow=dir+"/slow.sh";
        {
            std::ofstream stub(slow);
            stub<<"#!/bin/sh\nsleep 30\ncp '"+wav+"' \"$3\"\n";
        }
        ::chmod(slow.c_str(),0755);
        tri::musicgen::AudioCppCliBackend backend;
        tri::musicgen::GenRequest r;
        r.family="ace_step";r.execPath=slow;r.modelPath=dir;r.workDir=work;
        uint64_t task=0;std::string why;
        check(backend.submit(r,task,why),"slow request submits");
        backend.cancel(task);
        tri::musicgen::GenResult out;
        check(waitFor(backend,task,out,why)==tri::musicgen::GenTaskStatus::Cancelled,"cancel reports Cancelled");
    }
    // Missing model file fails with a clear reason.
    {
        const std::string work=dir+"/w-badmodel";::mkdir(work.c_str(),0755);
        tri::musicgen::AudioCppCliBackend backend;
        tri::musicgen::GenRequest r;
        r.family="ace_step";r.execPath=script;r.workDir=work;
        r.modelPath="/nonexistent/model";
        uint64_t task=0;std::string why;
        check(!backend.submit(r,task,why),"missing model is rejected up front");
        check(why.find("模型文件不存在")!=std::string::npos,"the reason names the model");
    }

    std::printf("ace-step tests %s\n",failures==0?"passed":"failed");
    return failures==0?0:1;
}
