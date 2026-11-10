#pragma once
#include "../../platform/NodeDescriptor.hpp"
// soundnet-module: soundnet.ace-step
// soundnet-order: 10
#include "Kernel.hpp"
namespace tri::nodes::ace_step {

// Playback and generation parameter IDs live in Kernel.hpp (included above).
inline constexpr ParamInfo gainInfo {"Playback gain",0,4,1,"x"};
inline constexpr ParamInfo loopInfo {"Loop",0,1,0,""};
inline constexpr ParamInfo startInfo{"Region start",0,1,0,""};
inline constexpr ParamInfo endInfo  {"Region end",0,1,1,""};
inline constexpr ParamInfo fadeInfo {"Fade",0,60,0.05,"s"};
inline constexpr ParamInfo secondsInfo{"Duration",1,600,30,"s"};
inline constexpr ParamInfo stepsInfo{"Inference steps",1,64,8,""};
inline constexpr ParamInfo seedInfo {"Seed",0,999999999.f,0,""};

inline bool validate(const KernelConfig& config,std::string& why) {
    if(config.value(startID,0)>=config.value(endID,1)) {
        why="播放区间的起点必须早于终点。";
        return false;
    }
    return true;
}

inline NodeModule module() {
    using namespace tri::detail;
    NodeSpec spec{"soundnet.ace-step",nodeTypeID("soundnet.ace-step"),"ACE-Step 生成","输入",1,1,true,true,
        CapTask|CapGenerator|CapAssetAudio|CapStream|CapBypassable,TimeMode::Timed,0,
        {pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional)},
        {pout("audio","音频",Signal::Audio,RoleMain)},
        {gainID,loopID,startID,endID,fadeID,secondsID,stepsID,seedID},
        "audio",
        {{"ace.prompt","描述","warm lo-fi hip hop with a soft rhodes piano, instrumental",4096,false,false,false},
         {"ace.lyrics","歌词","",65536,true,false,false},
         {"gen.extraArgs","附加参数","",4096,false,false,false},
         {"gen.execPath","后端可执行文件","",1024,false,false,false},
         {"gen.modelPath","模型目录","",1024,false,false,false}}};
    spec.kernelFactory=create;
    spec.validateConfig=validate;
    return {std::move(spec),{
        {gainID, ParamSpec{&gainInfo, "play.gain", false,nullptr,0,ParamGroup::Mix}},
        {loopID, ParamSpec{&loopInfo, "play.loop", true,nullptr,0,ParamGroup::Playback}},
        {startID,ParamSpec{&startInfo,"play.start",false,nullptr,0,ParamGroup::Playback}},
        {endID,  ParamSpec{&endInfo,  "play.end",  false,nullptr,0,ParamGroup::Playback}},
        {fadeID, ParamSpec{&fadeInfo, "play.fade", false,nullptr,0,ParamGroup::Playback}},
        {secondsID,ParamSpec{&secondsInfo,"gen.seconds",false,nullptr,0,ParamGroup::General}},
        {stepsID,ParamSpec{&stepsInfo,"gen.steps",true,nullptr,0,ParamGroup::General}},
        {seedID, ParamSpec{&seedInfo, "gen.seed",  true,nullptr,0,ParamGroup::General}}},
        {{"ACE-Step 生成","ACE-Step Generation"},{"输入","Input"},{"触发","Trigger"},{"音频","Audio"},
         {"描述","Prompt"},{"歌词","Lyrics"},{"附加参数","Extra arguments"},
         {"后端可执行文件","Backend executable"},{"模型目录","Model directory"},
         {"生成记录","Take log"},
         {"播放增益","Playback gain"},{"循环","Loop"},{"区间起点","Region start"},
         {"区间终点","Region end"},{"淡化","Fade"},{"时长","Duration"},
         {"推理步数","Inference steps"},{"种子","Seed"},
         {"生成","Generate"},{"停止","Stop"},{"正在生成…","Generating…"},{"空闲","Idle"},
         {"正在生成 %d%%","Generating %d%%"},{"正在取消生成…","Cancelling generation…"},
         {"生成记录","Take log"},{"选择记录…","Choose a take…"},
         {"试听（独立播放，不影响图）","Audition (independent of the graph)"},
         {"播放","Play"},{"暂停","Pause"},{"停止播放","Stop playback"},
         {"请先填写描述。","Enter a description first."},
         {"无法读取节点参数，请重新选择节点。","Couldn't read the node settings. Select the node again."},
         {"该记录的资源不可用。","The resources for this take are unavailable."},
         {"载入失败：文件缺失或已移动。","Load failed: the file is missing or moved."},
         {"还没有素材可试听。","Nothing to audition yet."},
         {"已载入所选记录。","Loaded the selected take."},
         {"试听不经过图；节点内播放由「触发」端口驱动，走音频节点输出。",
          "Audition bypasses the graph; in-graph playback is driven by the trigger port."},
         {"触发端口控制播放。保存工程时，音频与乐谱存放在独立的素材目录。",
          "The trigger port controls playback. Saved projects keep audio in a separate asset folder."},
         {"生成完成。","Generation complete."}}};
}
}
