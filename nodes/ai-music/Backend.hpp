#pragma once
// GenBackend — abstraction over a local music-generation runtime.
//
// A control-thread caller explicitly submits an immutable request through the
// platform TaskService. Audio callbacks only play already published resources.
// This module is free of Objective-C and can run in tests, CLI and host adapters.
//
// Default implementation: audio.cpp CLI (`audiocpp_cli`), a pure C++/ggml
// runtime with no Python dependency that serves both ACE-Step 1.5 and YuE2.
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>
#include <csignal>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "../../platform/TaskService.hpp"

extern char **environ;

namespace tri {

enum class GenTaskStatus { Running, Succeeded, Failed, Cancelled };

struct GenRequest {
    int backend=0;                 // 0 audio.cpp CLI, 1 audio.cpp server, 2 ComfyUI
    int model=0;                   // 0 ACE-Step, 1 YuE2 BF16, 2 YuE2 INT8, 3 YuE2 GGUF Q4
    int quality=1;                 // 0 fast, 1 balanced, 2 high
    int outMode=0;                 // 0 audio only, 1 audio + score, 2 score only
    double seconds=120;
    unsigned seed=0;
    std::string prompt,lyrics,execPath,modelPath,extraArgs,workDir;
    // When set, the backend re-renders from this score instead of planning a new
    // one (audio.cpp `abc_file`). This is the "edit the score, generate again"
    // half of the score loop.
    std::string abcText;
    std::string family() const {return model==0?"ace_step":"yue2";}
};

struct GenResult {
    std::string wavPath,abcPath,log;
};

class GenBackend {
public:
    virtual ~GenBackend()=default;
    virtual bool available(std::string& why)=0;
    virtual bool submit(const GenRequest& request,uint64_t& task,std::string& why)=0;
    virtual int progress(uint64_t task)=0;                  // 0..100, -1 unknown
    virtual void cancel(uint64_t task)=0;
    virtual GenTaskStatus poll(uint64_t task,GenResult& out,std::string& why)=0;
};

namespace detail {
inline bool fileExists(const std::string& path) {
    return !path.empty() && access(path.c_str(),R_OK)==0;
}
inline bool executable(const std::string& path) {
    return !path.empty() && access(path.c_str(),X_OK)==0;
}
inline std::string tailOf(const std::string& path,size_t bytes=2048) {
    std::ifstream in(path,std::ios::binary);
    if(!in)return {};
    in.seekg(0,std::ios::end);
    std::streamoff size=in.tellg();
    if(size<=0)return {};
    const std::streamoff start=size>std::streamoff(bytes)?size-std::streamoff(bytes):0;
    in.seekg(start);
    std::string text((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    return text;
}
} // namespace detail

// audio.cpp CLI backend. One child process per task; the process is killed on
// cancel, so a wedged model can never hold the editor hostage.
class AudioCppCliBackend final : public GenBackend {
public:
    ~AudioCppCliBackend() override {
        if(active) {
            cancel(serial);
            if(pid>0) {int status=0;while(::waitpid(pid,&status,0)<0&&errno==EINTR){};}
        }
    }
    bool available(std::string& why) override {
        if(detail::executable(configured)) {
            resolved=configured;
            return true;
        }
        if(!configured.empty()) {
            why="后端不可执行："+configured;
            return false;
        }
        // PATH lookup, no shell involved.
        const char* path=std::getenv("PATH");
        std::string list=path?path:"";
        std::stringstream stream(list);
        std::string dir;
        while(std::getline(stream,dir,':')) {
            if(dir.empty())continue;
            const std::string candidate=dir+"/audiocpp_cli";
            if(detail::executable(candidate)) {resolved=candidate;return true;}
        }
        why="未找到 audiocpp_cli。请先安装 audio.cpp，或在节点的「后端可执行文件」中填写完整路径。";
        return false;
    }
    bool submit(const GenRequest& request,uint64_t& task,std::string& why) override {
        if(request.backend!=0) {
            why="当前只支持 audio.cpp CLI 后端。";
            return false;
        }
        if(active) {
            why="已有生成任务在进行中。";
            return false;
        }
        std::string reason;
        configured=request.execPath;
        if(!available(reason)) {why=reason;return false;}
        if(request.workDir.empty()) {why="任务缺少工作目录。";return false;}
        if(!detail::fileExists(request.modelPath)) {
            why="模型文件不存在："+(request.modelPath.empty()?"（未填写）":request.modelPath);
            return false;
        }
        if(!request.abcText.empty()) {
            std::ofstream score(request.workDir+"/score_in.abc",std::ios::binary|std::ios::trunc);
            if(!score) {why="无法写入回喂乐谱。";return false;}
            score.write(request.abcText.data(),std::streamsize(request.abcText.size()));
            if(!score) {why="写入回喂乐谱失败。";return false;}
        }
        std::vector<std::string> args=buildArguments(request);
        std::vector<char*> argv;
        argv.reserve(args.size()+1);
        for(auto& arg:args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        const std::string logPath=request.workDir+"/generation.log";
        const int log=::open(logPath.c_str(),O_CREAT|O_WRONLY|O_TRUNC,0644);
        if(log<0) {why="无法创建任务日志。";return false;}
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions,log,STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions,log,STDERR_FILENO);
        pid_t child=0;
        posix_spawnattr_t attributes;
        posix_spawnattr_init(&attributes);
        posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&attributes,0);
        const int rc=posix_spawnp(&child,resolved.c_str(),&actions,&attributes,argv.data(),environ);
        posix_spawnattr_destroy(&attributes);
        posix_spawn_file_actions_destroy(&actions);
        ::close(log);
        if(rc!=0) {
            why=std::string("无法启动生成进程：")+std::strerror(rc);
            return false;
        }
        pid=child;
        current=request;
        logFile=logPath;
        cancelled=false;
        active=true;
        task=++serial;
        return true;
    }
    int progress(uint64_t task) override {
        if(!active||task!=serial)return -1;
        // audio.cpp reports work through its --log TRACE/TIMING stream (stage
        // boundaries, not per-step percentages; Metal memory-maps weights, so
        // there is no upload fraction either). Map the last stage marker seen
        // to a coarse percent. A "step n/m" fallback is kept for other tools.
        const std::string text=detail::tailOf(logFile,16384);
        int best=-1;
        auto seen=[&](const char* needle,int percent){
            if(text.find(needle)!=std::string::npos&&percent>best)best=percent;
        };
        seen("runtime.load.phase inspect",3);
        seen("runtime.load.phase load",6);
        seen("runtime.load.phase loaded",10);
        // YuE2: symbolic plan, then semantic/acoustic, then VAE.
        seen("plan.abc_decode_ms",25);
        seen("semantic.abc_generated_tokens",35);
        seen("ar.generate.total_ms",45);
        seen("nar.synthesize.total_ms",65);
        seen("vae_decode.",90);
        // ACE-Step: planner, then DiT diffusion, then VAE decode. Note that
        // "vae.decoder.weights" loads early, so the needle needs the dot.
        seen("planner.total_ms",35);
        seen("diffusion.total_ms",70);
        seen("vae.decode.",90);
        if(best>=0)return best;
        // Fallback: planner/denoise step counters, "step n/m" style.
        size_t cursor=0;
        while(cursor<text.size()) {
            const size_t at=text.find("step ",cursor);
            if(at==std::string::npos)break;
            const size_t slash=text.find('/',at+5);
            const size_t end=text.find_first_of("\n\r ",slash==std::string::npos?at+5:slash+1);
            if(slash!=std::string::npos) {
                const int done=std::atoi(text.c_str()+at+5);
                const int total=std::atoi(text.c_str()+slash+1);
                if(total>0&&done>=0&&done<=total) best=int(100.0*double(done)/double(total));
            } else if(end!=std::string::npos) {
                const int percent=std::atoi(text.c_str()+at+5);
                if(percent>=0&&percent<=100)best=percent;
            }
            cursor=at+5;
        }
        return best;
    }
    void cancel(uint64_t task) override {
        if(!active||task!=serial)return;
        cancelled=true;
        if(pid>0) {
            ::kill(-pid,SIGTERM);
            // Escalate after a short grace period; poll() reaps either way.
            for(int i=0;i<20;++i) {
                int status=0;
                const pid_t done=::waitpid(pid,&status,WNOHANG);
                if(done==pid) {pid=-1;return;}
                ::usleep(25000);
            }
            ::kill(-pid,SIGKILL);
        }
    }
    GenTaskStatus poll(uint64_t task,GenResult& out,std::string& why) override {
        if(!active||task!=serial) {
            why="没有可查询的任务。";
            return GenTaskStatus::Failed;
        }
        int status=0;
        const pid_t done=pid>0?::waitpid(pid,&status,WNOHANG):pid;
        if(done==0) return GenTaskStatus::Running;
        const int exitCode=(done==pid&&pid>0&&WIFEXITED(status))?WEXITSTATUS(status):-1;
        pid=-1;
        active=false;
        out.log=detail::tailOf(logFile);
        if(cancelled) {
            why="已取消生成。";
            return GenTaskStatus::Cancelled;
        }
        const std::string wav=current.workDir+"/out.wav";
        const std::string abc=current.workDir+"/score.abc";
        const bool wantWav=current.outMode!=2;
        if(exitCode!=0||(wantWav&&!detail::fileExists(wav))) {
            why="生成失败（退出码 "+std::to_string(exitCode)+"）。";
            return GenTaskStatus::Failed;
        }
        if(!wantWav&&!detail::fileExists(abc)) {
            why="后端没有输出乐谱（score.abc）。";
            return GenTaskStatus::Failed;
        }
        if(wantWav) out.wavPath=wav;
        if(detail::fileExists(abc)) out.abcPath=abc;
        return GenTaskStatus::Succeeded;
    }
    uint64_t currentTask() const {return serial;}

private:
    std::vector<std::string> buildArguments(const GenRequest& request) {
        std::vector<std::string> args;
        args.push_back(resolved);
        args.push_back("--task");args.push_back("gen");
        args.push_back("--family");args.push_back(request.family());
        args.push_back("--model");args.push_back(request.modelPath);
        args.push_back("--backend");args.push_back("metal");
        if(!request.lyrics.empty()) {
            args.push_back("--lyrics");args.push_back(request.lyrics);
        }
        if(request.model==0) {
            // ACE-Step (verified against audio.cpp 0.9.4 CLI): the prompt is
            // positional text, duration is explicit, and there is no planner.
            args.push_back("--text");args.push_back(request.prompt);
            args.push_back("--duration-seconds");
            args.push_back(std::to_string(request.seconds));
        } else {
            args.push_back("--request-option");args.push_back("style="+request.prompt);
            // External ABC forces the planner on: audio.cpp rejects abc_file
            // with cot=off ("external ABC requires cot=melody or cot=full").
            const bool cot= request.outMode>=1 || !request.abcText.empty();
            args.push_back("--request-option");
            args.push_back(cot?"cot=full":"cot=off");
            // YuE2 packages ship several main GGUFs in one directory; the
            // default session loads q8_0, so pin the file the node selected.
            args.push_back("--session-option");
            switch(request.model) {
                case 1: args.push_back("yue2.model_gguf=yue2-3b-bf16.gguf"); break;
                case 2: args.push_back("yue2.model_gguf=yue2-3b-q8_0.gguf"); break;
                default:args.push_back("yue2.model_gguf=yue2-3b-q4_0.gguf"); break;
            }
        }
        // Score-only runs stop after the symbolic stage. audio.cpp requires
        // stop_after=abc to have no external score (nothing left to plan),
        // so feed-back and score-only are mutually exclusive by design.
        if(request.outMode==2) {
            args.push_back("--request-option");
            args.push_back("stop_after=abc");
        }
        else if(!request.abcText.empty()) {
            args.push_back("--request-option");
            args.push_back("abc_file="+request.workDir+"/score_in.abc");
        }
        if(request.seed!=0) {args.push_back("--seed");args.push_back(std::to_string(request.seed));}
        // The stage markers our progress parser consumes come from this stream.
        args.push_back("--log");
        args.push_back("--load-progress");
        args.push_back("1");
        args.push_back("--out");args.push_back(request.workDir+"/out.wav");
        args.push_back("--out-dir");args.push_back(request.workDir);
        if(!request.extraArgs.empty()) {
            std::stringstream stream(request.extraArgs);
            std::string token;
            while(stream>>token)args.push_back(token);
        }
        return args;
    }
    std::string configured,resolved,logFile;
    GenRequest current;
    pid_t pid=-1;
    bool active=false,cancelled=false;
    uint64_t serial=0;
};

// Node-owned adapter over the platform task context. No editor or audio-engine
// state is touched here. Configuration is applied by submit before availability
// probing, so an explicit executable path works without a PATH installation.
struct GenerationOutcome {
    GenTaskStatus status=GenTaskStatus::Failed;
    GenResult result;
    std::string reason;
};
inline GenerationOutcome executeGeneration(GenBackend& backend,const GenRequest& request,
                                           soundnet::TaskContext& context,
                                           std::chrono::milliseconds timeout=std::chrono::minutes(30)) {
    GenerationOutcome outcome;
    if(context.cancelled()) {outcome.status=GenTaskStatus::Cancelled;return outcome;}
    uint64_t task=0;
    if(!backend.submit(request,task,outcome.reason))return outcome;
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    bool cancellationSent=false,timedOut=false;
    for(;;) {
        timedOut=timedOut || std::chrono::steady_clock::now()>=deadline;
        if((context.cancelled()||timedOut)&&!cancellationSent) {
            cancellationSent=true;backend.cancel(task);
        }
        outcome.status=backend.poll(task,outcome.result,outcome.reason);
        if(outcome.status!=GenTaskStatus::Running) {
            if(timedOut) {outcome.status=GenTaskStatus::Failed;outcome.reason="生成任务超时。";}
            else if(context.cancelled())outcome.status=GenTaskStatus::Cancelled;
            return outcome;
        }
        context.progress(backend.progress(task));
        // Once cancellation has been sent the backend must settle promptly.
        // Yield even for a cancelled token, whose wait would otherwise return
        // immediately and spin while an external process is terminating.
        if(cancellationSent)std::this_thread::sleep_for(std::chrono::milliseconds(20));
        else context.waitFor(std::chrono::milliseconds(100));
    }
}
} // namespace tri
