// Contract tests for CapTask nodes: descriptor shape, control surface and the
// audio.cpp CLI backend driven by a stub executable (no model required).
#include "../src/Engine.hpp"
#include "../src/GenBackend.hpp"
#include "../src/WavFile.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
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
tri::GraphSpec aiGraph() {
    tri::GraphSpec spec;
    tri::GraphNode node;
    node.id=1;node.type=tri::AiMusicNode;node.key=tri::nodeKey(tri::AiMusicNode);
    node.texts["ai.prompt"]="warm piano pop";
    spec.nodes.push_back(node);
    tri::GraphNode sink;
    sink.id=2;sink.type=tri::HardwareOutputNode;sink.key=tri::nodeKey(tri::HardwareOutputNode);
    spec.nodes.push_back(sink);
    tri::GraphEdge edge;edge.source=1;edge.target=2;
    edge.sourcePortId="audio";edge.portId="audio";
    spec.edges.push_back(edge);
    return spec;
}
} // namespace

int main() {
    // ---- descriptor -------------------------------------------------------
    const tri::NodeSpec* spec=tri::nodeSpec(tri::AiMusicNode);
    check(spec!=nullptr,"ai node registered");
    if(spec) {
        check(spec->type==tri::AiMusicNode,"ai node type is 47");
        check(tri::nodeCapabilities(spec->type)&tri::CapTask,"declares CapTask");
        check(tri::nodeCapabilities(spec->type)&tri::CapAssetAudio,"declares CapAssetAudio");
        check(tri::nodeInputByRole(spec->type,tri::RoleTrigger)>=0,"has a trigger input");
        check(tri::nodeTextCount(spec->type)==6,"declares six text parameters");
        check(std::string(tri::nodeTextSpec(spec->type,"ai.prompt")->id)=="ai.prompt","prompt text key");
        check(tri::nodeAcceptsParam(spec->type,tri::AiMusicDuration),"accepts duration");
        check(tri::nodeParamDiscrete(spec->type,tri::AiMusicModel),"model is discrete");
        check(!tri::nodeParamDiscrete(spec->type,tri::AiMusicDuration),"duration is continuous");
    }
    check(tri::registryProblems().empty(),"registry self-check");

    // ---- engine control surface ------------------------------------------
    {
        tri::Engine engine;
        check(engine.setGraph(aiGraph()),"graph publishes");
        auto tasks=engine.aiTasks();
        check(tasks.size()==1,"one task node snapshot");
        if(!tasks.empty()) {
            check(tasks[0].id==1,"snapshot carries the instance id");
            check(tasks[0].status==0,"starts idle");
            check(!tasks[0].hasSample,"starts without a sample");
        }
        check(engine.setAiTaskState(1,1,37),"state can be published");
        tasks=engine.aiTasks();
        check(!tasks.empty()&&tasks[0].status==1&&tasks[0].progress==37,"state round-trips");
        engine.cancelAiTask(1);
        tasks=engine.aiTasks();
        check(!tasks.empty()&&tasks[0].cancels==1,"cancel counter increments");
        engine.stopNode(1);   // must not crash and must not touch parameters
        engine.reset();
        tasks=engine.aiTasks();
        check(!tasks.empty()&&tasks[0].status==0,"reset returns the task to idle");
    }

    // ---- backend: success, failure, cancel -------------------------------
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
        tri::GenRequest r;
        r.execPath=exec;r.modelPath=wav;r.workDir=work;
        r.prompt="test";r.lyrics="la";r.seconds=1;r.seed=7;
        return r;
    };
    auto waitFor=[&](tri::AudioCppCliBackend& backend,uint64_t task,tri::GenResult& out,std::string& why) {
        for(int i=0;i<400;++i) {
            const auto status=backend.poll(task,out,why);
            if(status!=tri::GenTaskStatus::Running)return status;
            ::usleep(25000);
        }
        return tri::GenTaskStatus::Running;
    };
    {
        const std::string work=dir+"/task-ok";
        ::mkdir(work.c_str(),0755);
        tri::AudioCppCliBackend backend;
        uint64_t task=0;std::string why;
        check(backend.available(why)||true,"availability is advisory");
        check(backend.submit(request(goodScript,work),task,why),"stub submits");
        tri::GenResult out;
        const auto status=waitFor(backend,task,out,why);
        check(status==tri::GenTaskStatus::Succeeded,"stub succeeds");
        check(out.wavPath==work+"/out.wav","result path");
        check(tri::detail::fileExists(out.wavPath),"result exists");
    }
    {
        const std::string work=dir+"/task-bad";
        ::mkdir(work.c_str(),0755);
        tri::AudioCppCliBackend backend;
        uint64_t task=0;std::string why;
        check(backend.submit(request(badScript,work),task,why),"failing stub submits");
        tri::GenResult out;
        const auto status=waitFor(backend,task,out,why);
        check(status==tri::GenTaskStatus::Failed,"failing stub reports failure");
        check(!why.empty(),"failure carries a reason");
    }
    {
        const std::string work=dir+"/task-cancel";
        ::mkdir(work.c_str(),0755);
        tri::AudioCppCliBackend backend;
        uint64_t task=0;std::string why;
        check(backend.submit(request(slowScript,work),task,why),"slow stub submits");
        backend.cancel(task);
        tri::GenResult out;
        const auto status=waitFor(backend,task,out,why);
        check(status==tri::GenTaskStatus::Cancelled,"cancel is reported");
        check(!tri::detail::fileExists(work+"/out.wav"),"cancel writes no result");
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
        tri::AudioCppCliBackend backend;
        tri::GenRequest feed=request(script,work);
        feed.model=1;   // YuE2: the family with a symbolic planner
        feed.outMode=1;
        feed.abcText="X:1\nM:4/4\nL:1/4\nK:C\nC D E|\n";
        uint64_t task=0;std::string why;
        check(backend.submit(feed,task,why),"feed-back request submits");
        check(tri::detail::fileExists(work+"/score_in.abc"),"the score is written for the backend");
        check(readText(work+"/score_in.abc")==feed.abcText,"the score file matches the request");
        tri::GenResult out;
        check(waitFor(backend,task,out,why)==tri::GenTaskStatus::Succeeded,"feed-back run succeeds");
        const std::string arguments=readText(work+"/args.txt");
        check(arguments.find("abc_file="+work+"/score_in.abc")!=std::string::npos,
              "abc_file points at the written score");
        check(arguments.find("cot=full")!=std::string::npos,"asking for a score enables the planner");
        check(arguments.find("stop_after")==std::string::npos,"full runs never stop after the score");
    }
    // audio.cpp rejects an external score with cot=off; the backend must force
    // the planner on whenever a score is fed back, even in audio-only mode.
    {
        const std::string work=dir+"/task-feedaudio";
        ::mkdir(work.c_str(),0755);
        const std::string script=work+"/feedaudio.sh";
        {
            std::ofstream stub(script);
            stub<<"#!/bin/sh\nprintf '%s\\n' \"$@\" > '"<<work<<"/args.txt'\n"
                <<"out=\"\"\nwhile [ $# -gt 0 ]; do\n  if [ \"$1\" = \"--out\" ]; then out=\"$2\"; shift 2; else shift; fi\ndone\n"
                <<"cp '"<<wav<<"' \"$out\"\nexit 0\n";
        }
        ::chmod(script.c_str(),0755);
        tri::AudioCppCliBackend backend;
        tri::GenRequest feed=request(script,work);
        feed.model=1;
        feed.outMode=0;
        feed.abcText="X:1\nM:4/4\nL:1/4\nK:C\nC D E|\n";
        uint64_t task=0;std::string why;
        check(backend.submit(feed,task,why),"audio-only feed-back submits");
        tri::GenResult out;
        check(waitFor(backend,task,out,why)==tri::GenTaskStatus::Succeeded,"audio-only feed-back succeeds");
        const std::string arguments=readText(work+"/args.txt");
        check(arguments.find("cot=full")!=std::string::npos,"an external score forces cot=full");
        check(arguments.find("abc_file=")!=std::string::npos,"the score still reaches the backend");
        check(arguments.find("stop_after")==std::string::npos,"audio-only feed-back renders audio");
    }
    // ---- score-only mode: no WAV, the score is the deliverable ------------
    {
        const std::string work=dir+"/task-score";
        ::mkdir(work.c_str(),0755);
        const std::string script=work+"/score.sh";
        {
            std::ofstream stub(script);
            stub<<"#!/bin/sh\ndir=\"\"\nwhile [ $# -gt 0 ]; do\n  if [ \"$1\" = \"--out-dir\" ]; then dir=\"$2\"; shift 2; else shift; fi\ndone\n"
                <<"printf 'X:1\\nM:4/4\\nL:1/4\\nK:C\\nC D E F\\n' > \"$dir/score.abc\"\necho 'step 3/3'\nexit 0\n";
        }
        ::chmod(script.c_str(),0755);
        tri::AudioCppCliBackend backend;
        tri::GenRequest only=request(script,work);
        only.outMode=2;
        uint64_t task=0;std::string why;
        check(backend.submit(only,task,why),"score-only request submits");
        tri::GenResult out;
        check(waitFor(backend,task,out,why)==tri::GenTaskStatus::Succeeded,"score-only run succeeds without audio");
        check(out.wavPath.empty(),"score-only run reports no audio");
        check(out.abcPath==work+"/score.abc","score-only run reports the score");
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
        tri::AudioCppCliBackend backend;
        tri::GenRequest only=request(script,work);
        only.outMode=2;
        uint64_t task=0;std::string why;
        check(backend.submit(only,task,why),"score-only stub submits");
        tri::GenResult out;
        check(waitFor(backend,task,out,why)==tri::GenTaskStatus::Failed,"a missing score fails a score-only run");
        check(why.find("score.abc")!=std::string::npos,"the failure names the missing artifact");
    }
    std::printf("AiTaskTests %s\n",failures==0?"passed":"failed");
    return failures==0?0:1;
}
