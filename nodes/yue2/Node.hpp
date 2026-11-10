#pragma once
#include "../../platform/NodeDescriptor.hpp"
// soundnet-module: soundnet.yue2
// soundnet-order: 9
#include "Kernel.hpp"
namespace tri::nodes::yue2 {

// Playback parameter IDs live in Kernel.hpp (included above); generation
// parameter IDs are declared here.
inline constexpr int cotID        =parameterID("soundnet.yue2","gen.cot");
inline constexpr int outputID     =parameterID("soundnet.yue2","gen.output");
inline constexpr int variantID    =parameterID("soundnet.yue2","gen.variant");
inline constexpr int stepsID      =parameterID("soundnet.yue2","gen.steps");
inline constexpr int seedID       =parameterID("soundnet.yue2","gen.seed");

inline constexpr ParamInfo gainInfo {"Playback gain",0,4,1,"x"};
inline constexpr ParamInfo loopInfo {"Loop",0,1,0,""};
inline constexpr ParamInfo startInfo{"Region start",0,1,0,""};
inline constexpr ParamInfo endInfo  {"Region end",0,1,1,""};
inline constexpr ParamInfo fadeInfo {"Fade",0,60,0.05,"s"};
inline constexpr ParamInfo cotInfo  {"Planning (CoT)",0,2,2,""};
inline constexpr ParamInfo outputInfo{"Output",0,1,0,""};
inline constexpr ParamInfo variantInfo{"Precision",0,2,0,""};
inline constexpr ParamInfo stepsInfo{"Inference steps",1,64,8,""};
inline constexpr ParamInfo seedInfo {"Seed",0,999999999.f,0,""};

static const char* const cotOptions[]={"关闭","仅旋律","完整"};
static const char* const outputOptions[]={"音频","仅乐谱"};
static const char* const variantOptions[]={"Q4","Q8","BF16"};

inline bool validate(const KernelConfig& config,std::string& why) {
    if(config.value(startID,0)>=config.value(endID,1)) {
        why="播放区间的起点必须早于终点。";
        return false;
    }
    const float cot=config.value(cotID,2);
    const float score=config.value(outputID,0);
    if(score>0.5f && cot<0.5f) {
        why="仅乐谱模式需要启用符号规划（完整或仅旋律）。";
        return false;
    }
    return true;
}

inline NodeModule module() {
    using namespace tri::detail;
    NodeSpec spec{"soundnet.yue2",nodeTypeID("soundnet.yue2"),"YuE2 生成","输入",1,1,true,true,
        CapTask|CapGenerator|CapAssetAudio|CapStream|CapBypassable,TimeMode::Timed,0,
        {pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional)},
        {pout("audio","音频",Signal::Audio,RoleMain),
         pout("notes","MIDI 音符",Signal::Midi,RoleMain)},
        {gainID,loopID,startID,endID,fadeID,cotID,outputID,variantID,stepsID,seedID},
        "audio",
        {{"yue2.style","风格描述","English, indie pop, bright acoustic guitar, soft drums",4096,false,false,false},
         {"yue2.lyrics","歌词","",65536,true,false,false},
         {"gen.execPath","后端可执行文件","",1024,false,false,false},
         {"gen.modelPath","模型目录","",1024,false,false,false},
         {"gen.extraArgs","附加参数","",4096,false,false,false},
         {"yue2.arLora","AR LoRA 路径","",1024,false,false,false},
         {"yue2.narLora","NAR LoRA 路径","",1024,false,false,false},
         {"yue2.score","乐谱（ABC）","",262144,true,false,false,"abc"}}};
    spec.kernelFactory=create;
    spec.validateConfig=validate;
    return {std::move(spec),{
        {gainID, ParamSpec{&gainInfo, "play.gain", false,nullptr,0,ParamGroup::Mix}},
        {loopID, ParamSpec{&loopInfo, "play.loop", true,nullptr,0,ParamGroup::Playback}},
        {startID,ParamSpec{&startInfo,"play.start",false,nullptr,0,ParamGroup::Playback}},
        {endID,  ParamSpec{&endInfo,  "play.end",  false,nullptr,0,ParamGroup::Playback}},
        {fadeID, ParamSpec{&fadeInfo, "play.fade", false,nullptr,0,ParamGroup::Playback}},
        {cotID,  ParamSpec{&cotInfo,  "gen.cot",   true,cotOptions,3,ParamGroup::General}},
        {outputID,ParamSpec{&outputInfo,"gen.output",true,outputOptions,2,ParamGroup::General}},
        {variantID,ParamSpec{&variantInfo,"gen.variant",true,variantOptions,3,ParamGroup::General}},
        {stepsID,ParamSpec{&stepsInfo,"gen.steps",true,nullptr,0,ParamGroup::General}},
        {seedID, ParamSpec{&seedInfo, "gen.seed",  true,nullptr,0,ParamGroup::General}}},
        {{"YuE2 生成","YuE2 Generation"},{"输入","Input"},{"触发","Trigger"},{"音频","Audio"},{"MIDI 音符","MIDI Notes"},
         {"风格描述","Style"},{"歌词","Lyrics"},{"后端可执行文件","Backend executable"},
         {"模型目录","Model directory"},{"附加参数","Extra arguments"},
         {"AR LoRA 路径","AR LoRA path"},{"NAR LoRA 路径","NAR LoRA path"},
         {"乐谱（ABC）","Score (ABC)"},{"生成记录","Take log"},
         {"播放增益","Playback gain"},{"循环","Loop"},{"区间起点","Region start"},
         {"区间终点","Region end"},{"淡化","Fade"},{"推理步数","Inference steps"},{"种子","Seed"},
         {"规划深度","Planning (CoT)"},{"输出","Output"},{"精度","Precision"},
         {"关闭","Off"},{"仅旋律","Melody"},{"完整","Full"},{"仅乐谱","Score only"},
         {"生成","Generate"},{"停止","Stop"},{"正在生成…","Generating…"},{"空闲","Idle"},
         {"正在生成 %d%%","Generating %d%%"},{"正在取消生成…","Cancelling generation…"},
         {"生成记录","Take log"},{"选择记录…","Choose a take…"},
         {"试听（独立播放，不影响图）","Audition (independent of the graph)"},
         {"播放","Play"},{"暂停","Pause"},{"停止播放","Stop playback"},
         {"请先填写风格描述。","Enter a style description first."},
         {"无法读取节点参数，请重新选择节点。","Couldn't read the node settings. Select the node again."},
         {"乐谱为空，请先生成或粘贴 ABC 文本。","The score is empty; generate or paste ABC text first."},
         {"该记录的资源不可用。","The resources for this take are unavailable."},
         {"载入失败：文件缺失或已移动。","Load failed: the file is missing or moved."},
         {"还没有素材可试听。","Nothing to audition yet."},
         {"已载入所选记录。","Loaded the selected take."},
         {"试听不经过图；节点内播放由「触发」端口驱动，走音频节点输出。",
          "Audition bypasses the graph; in-graph playback is driven by the trigger port."},
         {"按乐谱重新生成","Regenerate from score"},{"导入为 MIDI 剪辑","Import as MIDI clip"},
         {"乐谱无法转换为 MIDI 剪辑。","The score could not be converted to a MIDI clip."},
         {"MIDI 剪辑速度必须在 40–240 BPM 之间。","MIDI clip tempo must be between 40 and 240 BPM."},
         {"触发端口控制播放。保存工程时，音频与乐谱存放在独立的素材目录。",
          "The trigger port controls playback. Saved projects keep audio and scores in a separate asset folder."},
         {"生成完成。","Generation complete."},
         {"乐谱已写入「乐谱（ABC）」。","The score was written to “Score (ABC)”."}}};
}
}
