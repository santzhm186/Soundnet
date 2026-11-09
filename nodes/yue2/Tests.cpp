#include "Kernel.hpp"
#include "Node.hpp"
#include "../../platform/musicgen/MusicGenBackend.hpp"
#include "../../platform/TaskService.hpp"
#include "AbcScore.hpp"
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
        const auto& module=tri::nodes::yue2::module();
        const tri::NodeSpec& spec=module.spec;
        check(std::strcmp(spec.key,"soundnet.yue2")==0,"yue2 key");
        check(spec.capabilities&tri::CapTask,"yue2 declares a task");
        check(spec.assetKind&&std::strcmp(spec.assetKind,"audio")==0,"yue2 embeds audio");
        check(!spec.legacyAdapter,"yue2 runs on the public kernel path");
        check(spec.inputs.size()==1&&spec.inputs[0].signal==tri::Signal::Trigger,"yue2 trigger input");
        check(spec.outputs.size()==1&&spec.outputs[0].signal==tri::Signal::Audio,"yue2 audio output");
        check(spec.kernelFactory!=nullptr,"yue2 has a kernel factory");
        check(module.parameters.size()==10,"yue2 parameter count");
        bool takes=false;
        for(const auto& text:spec.texts)if(std::strcmp(text.id,"gen.takes")==0)takes=text.hidden;
        check(takes,"the take log is hidden");
        tri::KernelConfig good;
        good.parameters[tri::nodes::yue2::startID]=0.2f;
        good.parameters[tri::nodes::yue2::endID]=0.9f;
        std::string why;
        check(tri::nodes::yue2::validate(good,why),"valid region passes");
        good.parameters[tri::nodes::yue2::endID]=0.1f;
        check(!tri::nodes::yue2::validate(good,why),"inverted region is rejected");
        good.parameters[tri::nodes::yue2::endID]=1.f;
        good.parameters[tri::nodes::yue2::outputID]=1.f;   // score only
        good.parameters[tri::nodes::yue2::cotID]=0.f;      // planning off
        check(!tri::nodes::yue2::validate(good,why),"score only requires planning");
    }

    // ---- playback kernel --------------------------------------------------
    {
        auto kernel=std::unique_ptr<tri::NodeKernel>(tri::nodes::yue2::create());
        kernel->prepare(48000,256);
        tri::KernelConfig config;
        auto sample=rampSample(48000);
        config.audio=sample;
        config.parameters[tri::nodes::yue2::gainID]=1.f;
        config.parameters[tri::nodes::yue2::fadeID]=0.f;   // hard edges for assertions
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

        // Trigger stop fades out (fade is zero here: immediate silence).
        tri::KernelTriggerEvent stop{0,false};
        trigger.triggers=&stop;trigger.triggerCount=1;
        context.inputs=&trigger;context.inputCount=1;
        kernel->reset(tri::KernelResetReason::Stop);
        runPlayback(*kernel,context);
        check(outL[255]==0,"stop resets playback");

        // Region bounds: end 0.0005 of a 48000-frame sample = 24 frames.
        config.parameters[tri::nodes::yue2::endID]=0.0005f;
        kernel->configure(config);
        events[0]={0,true};trigger.triggers=events;trigger.triggerCount=1;
        runPlayback(*kernel,context);
        bool tail=true;
        for(int i=30;i<256;++i)if(outL[i]!=0)tail=false;
        check(tail,"playback stops at the region end");

        // Loop wraps inside the region.
        config.parameters[tri::nodes::yue2::endID]=0.0005f;
        config.parameters[tri::nodes::yue2::loopID]=1.f;
        kernel->configure(config);
        events[0]={0,true};
        runPlayback(*kernel,context);
        check(outL[250]>0||outL[255]>0,"loop keeps producing audio past the region end");

        // Bypass silences and clears state.
        context.bypassed=true;
        runPlayback(*kernel,context);
        check(outL[255]==0,"bypass silences the node");
        context.bypassed=false;

        // Fade-in attack: first frame after start is below the sample value.
        config.parameters[tri::nodes::yue2::loopID]=0.f;
        config.parameters[tri::nodes::yue2::endID]=1.f;
        config.parameters[tri::nodes::yue2::fadeID]=0.01f;  // 480 samples
        kernel->configure(config);
        events[0]={0,true};
        runPlayback(*kernel,context);
        check(outL[1]<outL[2]&&outL[2]>0,"fade-in ramps the attack");

        // Empty sample: silence, no crash.
        tri::KernelConfig empty;
        kernel->configure(empty);
        runPlayback(*kernel,context);
        check(outL[255]==0,"missing asset stays silent");
    }

    // ---- backend contract -------------------------------------------------
    const std::string dir="/tmp/yue2-node-tests";
    ::mkdir(dir.c_str(),0755);
    const std::string wav=dir+"/stub.wav";
    {
        std::ofstream out(wav,std::ios::binary);
        out<<"RIFF----WAVEfmt ";  // content irrelevant; only existence matters
    }
    auto request=[&](const std::string& script,const std::string& work) {
        tri::musicgen::GenRequest r;
        r.family="yue2";
        r.execPath=script;r.modelPath=dir;r.workDir=work;
        return r;
    };
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

    // Argument contract: defaults pin q4, full planning, steps and seed.
    {
        const std::string work=dir+"/w-defaults";::mkdir(work.c_str(),0755);
        tri::musicgen::AudioCppCliBackend backend;
        tri::musicgen::GenRequest r=request(script,work);
        r.prompt="English, indie pop";
        r.steps=12;r.seed=99;r.variant=1;
        uint64_t task=0;std::string why;
        check(backend.submit(r,task,why),"yue2 request submits");
        tri::musicgen::GenResult out;
        check(waitFor(backend,task,out,why)==tri::musicgen::GenTaskStatus::Succeeded,"yue2 stub succeeds");
        const std::string arguments=readText(dir+"/args.txt");
        check(arguments.find("style=English, indie pop")!=std::string::npos,"style reaches the CLI");
        check(arguments.find("cot=full")!=std::string::npos,"default planning is full");
        check(arguments.find("yue2.model_gguf=yue2-3b-q8_0.gguf")!=std::string::npos,"variant pins the GGUF");
        check(arguments.find("num_inference_steps=12")!=std::string::npos,"steps reach the CLI");
        // The stub records one argument per line, so the pair spans a newline.
        check(arguments.find("--seed\n99")!=std::string::npos,"seed reaches the CLI");
        check(arguments.find("--lyrics")==std::string::npos,"empty lyrics are omitted");
        check(arguments.find("--text")==std::string::npos,"yue2 never uses the ACE text flag");
        check(!out.wavPath.empty(),"audio path is reported");
    }
    // Score feed-back forces the planner on and writes the score file.
    {
        const std::string work=dir+"/w-feed";::mkdir(work.c_str(),0755);
        tri::musicgen::AudioCppCliBackend backend;
        tri::musicgen::GenRequest r=request(script,work);
        r.cot=0;  // user turned planning off...
        r.abcText="X:1\nM:4/4\nL:1/4\nK:C\nC D E F\n";
        uint64_t task=0;std::string why;
        check(backend.submit(r,task,why),"feed-back request submits");
        check(readText(work+"/score_in.abc")==r.abcText,"the score file matches the request");
        tri::musicgen::GenResult out;
        check(waitFor(backend,task,out,why)==tri::musicgen::GenTaskStatus::Succeeded,"feed-back run succeeds");
        const std::string arguments=readText(dir+"/args.txt");
        check(arguments.find("abc_file="+work+"/score_in.abc")!=std::string::npos,"abc_file points at the score");
        check(arguments.find("cot=full")!=std::string::npos,"an external score forces cot=full");
        check(arguments.find("stop_after")==std::string::npos,"full runs never stop after the score");
    }
    // Score-only: no WAV required, stop_after=abc, external score rejected.
    {
        const std::string work=dir+"/w-scoreonly";::mkdir(work.c_str(),0755);
        const std::string scoreScript=dir+"/score.sh";
        {
            std::ofstream stub(scoreScript);
            stub<<"#!/bin/sh\nprintf '%s\\n' \"$@\" > '"+dir+"/args.txt'\ndir=\"\"\nwhile [ $# -gt 0 ]; do\n"
                <<"  if [ \"$1\" = \"--out-dir\" ]; then dir=\"$2\"; shift 2; else shift; fi\ndone\n"
                <<"printf 'X:1\\nM:4/4\\nL:1/4\\nK:C\\nC D E F\\n' > \"$dir/score.abc\"\nexit 0\n";
        }
        ::chmod(scoreScript.c_str(),0755);
        tri::musicgen::AudioCppCliBackend backend;
        tri::musicgen::GenRequest r=request(scoreScript,work);
        r.outMode=1;
        uint64_t task=0;std::string why;
        check(backend.submit(r,task,why),"score-only request submits");
        tri::musicgen::GenResult out;
        check(waitFor(backend,task,out,why)==tri::musicgen::GenTaskStatus::Succeeded,
              "score-only run succeeds without audio");
        check(out.wavPath.empty(),"score-only reports no audio");
        check(out.abcPath==work+"/score.abc","score-only reports the score");
        const std::string arguments=readText(dir+"/args.txt");
        check(arguments.find("stop_after=abc")!=std::string::npos,"score-only stops after the symbolic stage");
        tri::musicgen::GenRequest conflict=request(script,work);
        conflict.outMode=1;conflict.abcText="X:1\nK:C\n";
        uint64_t unused=0;
        check(!backend.submit(conflict,unused,why),"score-only with an external score is rejected");
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
        tri::musicgen::GenRequest r=request(slow,work);
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
        tri::musicgen::GenRequest r=request(script,work);
        r.modelPath="/nonexistent/model";
        uint64_t task=0;std::string why;
        check(!backend.submit(r,task,why),"missing model is rejected up front");
        check(why.find("模型文件不存在")!=std::string::npos,"the reason names the model");
    }

    // ---- ABC → MIDI conversion -------------------------------------------
    {
        tri::abc::Score score;std::string why;
        const char* text="X:1\nT:Test\nM:4/4\nL:1/4\nQ:1/4=120\nK:G\nG A B c | d2 B2 | z4 |";
        check(tri::abc::parse(text,score,why),"ABC parses");
        check(score.notes.size()>=6,"notes are extracted");
        check(score.bpm==120,"tempo is parsed");
        if(!score.notes.empty())
            check(score.notes[0].pitch==67,"G above middle C in G major");
        check(score.warnings.empty()||score.warnings.size()<3,"clean input has few warnings");
        // A score without a key signature is rejected.
        tri::abc::Score broken;std::string reason;
        check(!tri::abc::parse("C D E\n",broken,reason),"missing K: fails");
        check(reason.find("K:")!=std::string::npos,"the diagnostic names the missing key");
    }

    std::printf("yue2 tests %s\n",failures==0?"passed":"failed");
    return failures==0?0:1;
}
