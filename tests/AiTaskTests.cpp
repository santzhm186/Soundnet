// Contract tests for the generation nodes: descriptor shape through the
// registry and the shared audio.cpp CLI backend driven by a stub executable
// (no model required). The per-node suites in nodes/yue2 and nodes/ace-step
// cover the module-local surface; this file checks the integrated platform.
#include "../src/Engine.hpp"
#include "../platform/musicgen/MusicGenBackend.hpp"
#include "../platform/TaskService.hpp"
#include "../src/WavFile.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
int failures=0;
void check(bool value,const char* what) {
    if(!value) {std::printf("FAIL %s\n",what);++failures;}
}
std::string tempDir(const char* name) {
    char pattern[512];
    std::snprintf(pattern,sizeof(pattern),"/tmp/soundnet-%s-XXXXXX",name);
    return std::string(mkdtemp(pattern));
}
std::string readText(const std::string& path) {
    std::ifstream in(path);
    if(!in)return {};
    return std::string((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
}
tri::GraphSpec generationGraph(const char* key) {
    tri::GraphSpec spec;
    tri::GraphNode node;
    node.id=1;node.key=key;node.type=tri::nodeTypeForKey(key);
    const tri::NodeSpec* module=tri::nodeSpec(node.type);
    node.nodeVersion=module->nodeVersion;
    if(std::strcmp(key,"soundnet.yue2")==0)node.texts["yue2.style"]="warm piano pop";
    spec.nodes.push_back(node);
    tri::GraphNode sink;
    sink.id=2;sink.type=tri::AudioOutputNode;sink.nodeVersion=tri::nodeSpec(sink.type)->nodeVersion;sink.key=tri::nodeKey(tri::AudioOutputNode);
    spec.nodes.push_back(sink);
    tri::GraphEdge edge;edge.source=1;edge.target=2;
    edge.sourcePortId="audio";edge.portId="audio";
    spec.edges.push_back(edge);
    return spec;
}
} // namespace

int main() {
    // ---- descriptors -------------------------------------------------------
    const tri::NodeSpec* yue2=tri::nodeSpecForKey("soundnet.yue2");
    const tri::NodeSpec* ace=tri::nodeSpecForKey("soundnet.ace-step");
    check(yue2!=nullptr,"yue2 registered");
    check(ace!=nullptr,"ace-step registered");
    check(yue2&&ace&&yue2->type!=ace->type,"the two models are separate nodes");
    if(yue2) {
        check(tri::nodeCapabilities(yue2->type)&tri::CapTask,"yue2 declares CapTask");
        check(tri::nodeCapabilities(yue2->type)&tri::CapAssetAudio,"yue2 declares CapAssetAudio");
        check(tri::nodeInputByRole(yue2->type,tri::RoleTrigger)>=0,"yue2 has a trigger input");
        check(tri::nodeTextSpec(yue2->type,"yue2.score")!=nullptr,"yue2 carries the score text");
        check(tri::nodeTextSpec(yue2->type,"gen.takes")!=nullptr,"yue2 carries the take log");
        check(tri::nodeTextSpec(yue2->type,"gen.takes")->hidden,"the take log is hidden");
        check(tri::nodeAcceptsParam(yue2->type,tri::nodeParamIdForKey(yue2->type,"gen.steps")),"accepts steps");
        check(tri::nodeParamDiscrete(yue2->type,tri::nodeParamIdForKey(yue2->type,"gen.variant")),"variant is discrete");
        check(tri::nodeParamDiscrete(yue2->type,tri::nodeParamIdForKey(yue2->type,"gen.steps")),"steps are integral");
    }
    if(ace) {
        check(tri::nodeCapabilities(ace->type)&tri::CapTask,"ace-step declares CapTask");
        check(tri::nodeCapabilities(ace->type)&tri::CapAssetAudio,"ace-step declares CapAssetAudio");
        check(tri::nodeTextSpec(ace->type,"gen.takes")!=nullptr,"ace-step carries the take log");
    }
    check(tri::nodeSpecForKey("soundnet.ai-music")==nullptr,"the legacy ai-music node is gone");
    check(tri::registryProblems().empty(),"registry self-check");

    // Task ownership lives in the control-thread TaskService, not in audio
    // runtime shadow state. Playback remains an ordinary graph operation.
    {
        auto engine=std::make_unique<tri::Engine>();
        check(engine->setGraph(generationGraph("soundnet.yue2")),"yue2 graph publishes");
        engine->stopNode(1);
        check(engine->setGraph(generationGraph("soundnet.ace-step")),"ace-step graph publishes");
        engine->stopNode(1);
    }

    // ---- backend: success, failure, cancel, family checks ------------------
    const std::string dir=tempDir("ai");
    const std::string wav=dir+"/source.wav";
    {
        std::vector<float> left(4800,0.1f),right(4800,0.1f);
        std::string error;
        check(tri::wav::write(wav,left,right,48000,error,16),"stub wav written");
    }
    const std::string goodScript=dir+"/good.sh";
    {
        std::ofstream script(goodScript);
        script<<"#!/bin/sh\nout=\"\"\nwhile [ $# -gt 0 ]; do\n  if [ \"$1\" = \"--out\" ]; then out=\"$2\"; shift 2; else shift; fi\ndone\n"
              <<"sleep 0.2\ncp '"<<wav<<"' \"$out\"\necho 'step 5/10'\nexit 0\n";
    }
    const std::string badScript=dir+"/bad.sh";
    {
        std::ofstream script(badScript);
        script<<"#!/bin/sh\necho 'model not found' 1>&2\nexit 3\n";
    }
    const std::string slowScript=dir+"/slow.sh";
    {
        std::ofstream script(slowScript);
        script<<"#!/bin/sh\nsleep 30\nexit 0\n";
    }
    ::chmod(goodScript.c_str(),0755);
    ::chmod(badScript.c_str(),0755);
    ::chmod(slowScript.c_str(),0755);

    auto request=[&](const std::string& exec,const std::string& work) {
        tri::musicgen::GenRequest r;
        r.family="yue2";
        r.execPath=exec;r.modelPath=wav;r.workDir=work;
        r.prompt="test";r.lyrics="la";r.seconds=1;r.seed=7;
        return r;
    };
    using tri::musicgen::GenTaskStatus;
    auto waitFor=[](tri::musicgen::AudioCppCliBackend& backend,uint64_t task,
                    tri::musicgen::GenResult& out,std::string& why) {
        for(int i=0;i<400;++i) {
            const auto status=backend.poll(task,out,why);
            if(status!=GenTaskStatus::Running)return status;
            ::usleep(25000);
        }
        return GenTaskStatus::Running;
    };
    {
        tri::musicgen::AudioCppCliBackend backend;
        auto unsupported=request(goodScript,dir);unsupported.family="comfyui";
        uint64_t task=0;std::string why;
        check(!backend.submit(unsupported,task,why),"unsupported family cannot submit");
        check(!why.empty(),"unsupported family reports its limitation");
    }
    {
        const std::string work=dir+"/task-ok";
        ::mkdir(work.c_str(),0755);
        tri::musicgen::AudioCppCliBackend backend;
        uint64_t task=0;std::string why;
        // submit applies the explicit executable path before probing availability.
        check(backend.submit(request(goodScript,work),task,why),"stub submits");
        tri::musicgen::GenResult out;
        const auto status=waitFor(backend,task,out,why);
        check(status==GenTaskStatus::Succeeded,"stub succeeds");
        check(out.wavPath==work+"/out.wav","result path");
        check(tri::musicgen::detail::fileExists(out.wavPath),"result exists");
    }
    {
        const std::string work=dir+"/task-bad";
        ::mkdir(work.c_str(),0755);
        tri::musicgen::AudioCppCliBackend backend;
        uint64_t task=0;std::string why;
        check(backend.submit(request(badScript,work),task,why),"failing stub submits");
        tri::musicgen::GenResult out;
        const auto status=waitFor(backend,task,out,why);
        check(status==GenTaskStatus::Failed,"failing stub reports failure");
        check(!why.empty(),"failure carries a reason");
    }
    {
        const std::string work=dir+"/task-cancel";
        ::mkdir(work.c_str(),0755);
        tri::musicgen::AudioCppCliBackend backend;
        uint64_t task=0;std::string why;
        check(backend.submit(request(slowScript,work),task,why),"slow stub submits");
        backend.cancel(task);
        tri::musicgen::GenResult out;
        const auto status=waitFor(backend,task,out,why);
        check(status==GenTaskStatus::Cancelled,"cancel is reported");
        check(!tri::musicgen::detail::fileExists(work+"/out.wav"),"cancel writes no result");
    }
    // The same job adapter used by the node editors works with the platform
    // service. No preflight PATH lookup is required for a configured executable.
    {
        const std::string work=dir+"/task-service";
        ::mkdir(work.c_str(),0755);
        struct Payload final:soundnet::TaskPayload {tri::musicgen::GenerationOutcome outcome;};
        soundnet::TaskService service(1);
        service.reconcile({{7,1}});
        soundnet::TaskBinding binding;std::string why;
        check(service.submit(7,1,[&](soundnet::TaskContext& context) {
            tri::musicgen::AudioCppCliBackend backend;
            auto payload=std::make_shared<Payload>();
            payload->outcome=tri::musicgen::executeGeneration(backend,request(goodScript,work),context);
            return soundnet::TaskResult{soundnet::TaskStatus::Succeeded,payload,{}};
        },binding,why),"task service accepts generation adapter");
        check(service.waitIdle(std::chrono::seconds(5)),"generation adapter settles");
        const auto results=service.takeCompleted();
        check(results.size()==1,"generation publishes exactly one current result");
        if(results.size()==1) {
            auto payload=std::dynamic_pointer_cast<const Payload>(results[0].result.payload);
            check(payload&&payload->outcome.status==GenTaskStatus::Succeeded,"explicit backend path succeeds through coordinator");
        }
        check(service.takeCompleted().empty(),"generation result is consumed once");
    }
    // ---- score feed-back: the edited ABC reaches the backend --------------
    {
        const std::string work=dir+"/task-args";
        ::mkdir(work.c_str(),0755);
        const std::string script=work+"/args.sh";
        {
            std::ofstream stub(script);
            stub<<"#!/bin/sh\nprintf '%s\\n' \"$@\" > '"<<work<<"/args.txt'\n"
                <<"out=\"\"\nwhile [ $# -gt 0 ]; do\n  if [ \"$1\" = \"--out\" ]; then out=\"$2\"; shift 2; else shift; fi\ndone\n"
                <<"cp '"<<wav<<"' \"$out\"\nexit 0\n";
        }
        ::chmod(script.c_str(),0755);
        tri::musicgen::AudioCppCliBackend backend;
        tri::musicgen::GenRequest feed=request(script,work);
        feed.cot=0;  // the planner is off...
        feed.abcText="X:1\nM:4/4\nL:1/4\nK:C\nC D E|\n";  // ...but an external score forces it on
        uint64_t task=0;std::string why;
        check(backend.submit(feed,task,why),"feed-back request submits");
        check(tri::musicgen::detail::fileExists(work+"/score_in.abc"),"the score is written for the backend");
        check(readText(work+"/score_in.abc")==feed.abcText,"the score file matches the request");
        tri::musicgen::GenResult out;
        check(waitFor(backend,task,out,why)==GenTaskStatus::Succeeded,"feed-back run succeeds");
        const std::string arguments=readText(work+"/args.txt");
        check(arguments.find("abc_file="+work+"/score_in.abc")!=std::string::npos,
              "abc_file points at the written score");
        check(arguments.find("cot=full")!=std::string::npos,"an external score forces cot=full");
        check(arguments.find("stop_after")==std::string::npos,"full runs never stop after the score");
    }
    // ---- score-only mode: no WAV, the score is the deliverable ------------
    {
        const std::string work=dir+"/task-score";
        ::mkdir(work.c_str(),0755);
        const std::string script=work+"/score.sh";
        {
            std::ofstream stub(script);
            stub<<"#!/bin/sh\nprintf '%s\\n' \"$@\" > '"<<work<<"/args.txt'\ndir=\"\"\nwhile [ $# -gt 0 ]; do\n  if [ \"$1\" = \"--out-dir\" ]; then dir=\"$2\"; shift 2; else shift; fi\ndone\n"
                <<"printf 'X:1\\nM:4/4\\nL:1/4\\nK:C\\nC D E F\\n' > \"$dir/score.abc\"\necho 'step 3/3'\nexit 0\n";
        }
        ::chmod(script.c_str(),0755);
        tri::musicgen::AudioCppCliBackend backend;
        tri::musicgen::GenRequest only=request(script,work);
        only.outMode=1;
        uint64_t task=0;std::string why;
        check(backend.submit(only,task,why),"score-only request submits");
        tri::musicgen::GenResult out;
        check(waitFor(backend,task,out,why)==GenTaskStatus::Succeeded,"score-only run succeeds without audio");
        check(out.wavPath.empty(),"score-only run reports no audio");
        check(out.abcPath==work+"/score.abc","score-only run reports the score");
        const std::string arguments=readText(work+"/args.txt");
        check(arguments.find("stop_after=abc")!=std::string::npos,"score-only stops after the symbolic stage");
    }
    {
        const std::string work=dir+"/task-noscore";
        ::mkdir(work.c_str(),0755);
        const std::string script=work+"/empty.sh";
        {
            std::ofstream stub(script);
            stub<<"#!/bin/sh\nexit 0\n";
        }
        ::chmod(script.c_str(),0755);
        tri::musicgen::AudioCppCliBackend backend;
        tri::musicgen::GenRequest only=request(script,work);
        only.outMode=1;
        uint64_t task=0;std::string why;
        check(backend.submit(only,task,why),"score-only stub submits");
        tri::musicgen::GenResult out;
        check(waitFor(backend,task,out,why)==GenTaskStatus::Failed,"a missing score fails a score-only run");
        check(why.find("score.abc")!=std::string::npos,"the failure names the missing artifact");
    }
    std::printf("AiTaskTests %s\n",failures==0?"passed":"failed");
    return failures==0?0:1;
}
