#pragma once
// Single source of truth for node identity, menu placement and inspector
// parameters. This header is deliberately UI-free so the audio engine, the
// document validator and both editors can share it.
#include "LegacyEngine.hpp"
#include <vector>

namespace tri {
// Graph-only node IDs. They live above the legacy Node enum; Osc2/Osc3 remain
// migration-only and are never valid graph instances.
inline constexpr int HardwareOutputNode=19, DawOutputNode=20, DawClockNode=21, DawAudioNode=22;
inline constexpr int StartNode=23, StopNode=24, WaitNode=25, CountNode=26, ArrangerNode=27;
// Audio processors added in 2.2. Their IDs live above the legacy Node enum and
// their parameters live above the legacy Param range.
inline constexpr int FilterNode=28, ReverbNode=29, NoiseNode=30;
inline constexpr int FilterType=1100, FilterCutoff=1101, FilterResonance=1102, FilterDrive=1103;
inline constexpr int ReverbMix=1110, ReverbSize=1111, ReverbDamping=1112, ReverbWidth=1113;
inline constexpr int NoiseLevel=1120, NoiseType=1121;
// Arranger parameters are stored on the instance, outside the AU parameter list.
enum ArrangerParam {
    ArrHighMin=1024, ArrHighMax, ArrHighTexture,
    ArrMidMin, ArrMidMax, ArrMidTexture,
    ArrLowMin, ArrLowMax, ArrLowTexture
};

inline constexpr ParamInfo filterTypeInfo   {"Type",0,2,0,""};
inline constexpr ParamInfo filterCutoffInfo {"Cutoff",20,20000,8000,"Hz"};
inline constexpr ParamInfo filterResonanceInfo {"Resonance",.5f,20,.9f,"Q"};
inline constexpr ParamInfo filterDriveInfo  {"Drive",0,1,0,""};
inline constexpr ParamInfo reverbMixInfo    {"Mix",0,1,.25f,""};
inline constexpr ParamInfo reverbSizeInfo   {"Size",0,1,.55f,""};
inline constexpr ParamInfo reverbDampingInfo{"Damping",0,1,.4f,""};
inline constexpr ParamInfo reverbWidthInfo  {"Width",0,1,.9f,""};
inline constexpr ParamInfo noiseLevelInfo   {"Level",0,1,.2f,""};
inline constexpr ParamInfo noiseTypeInfo    {"Type",0,1,0,""};

struct NodeMeta {
    int type;
    const char* title;
    const char* category;
    bool visibleInCatalog;
    bool bypassable;
};

inline const NodeMeta* nodeMeta(int type) {
    static const NodeMeta nodes[] = {
        {MidiNode,"本地 MIDI","输入",true,false},
        {DawMidiNode,"DAW MIDI","输入",true,false},
        {ClipNode,"MIDI Clip","输入",true,false},
        {InputNode,"硬件音频输入","输入",true,false},
        {DawAudioNode,"DAW 音频输入","输入",true,false},
        {ChordNode,"和弦输入","输入",true,false},
        {ProgressNode,"和声走向","输入",true,false},
        {ClockNode,"节奏速度","输入",true,false},
        {DawClockNode,"DAW 时钟","输入",true,false},
        {Osc1Node,"振荡器","处理",true,false},
        {ArpNode,"ARP","处理",true,false},
        {ArrangerNode,"三声部编排器","处理",true,false},
        {VelocityNode,"力度自动化","处理",true,false},
        {ModelNode,"音符模型","处理",true,false},
        {MixNode,"混音器","处理",true,false},
        {EQNode,"EQ","处理",true,true},
        {DelayNode,"Delay","处理",true,true},
        {FilterNode,"滤波器","处理",true,true},
        {ReverbNode,"混响","处理",true,true},
        {NoiseNode,"噪声","处理",true,true},
        {StartNode,"开始触发器","逻辑",true,false},
        {StopNode,"停止触发器","逻辑",true,false},
        {WaitNode,"等待","逻辑",true,false},
        {CountNode,"次数条件","逻辑",true,false},
        {HardwareOutputNode,"硬件音频输出","输出",true,true},
        {DawOutputNode,"DAW 音频输出","输出",true,true},
        {MidiOutNode,"MIDI 输出","输出",true,false},
        {OutputNode,"音频输出","输出",false,true},
    };
    for(const auto& node:nodes)if(node.type==type)return &node;
    return nullptr;
}

inline const char* nodeTitle(int type) {
    const NodeMeta* meta=nodeMeta(type);
    return meta?meta->title:"节点";
}
inline const char* nodeCategory(int type) {
    const NodeMeta* meta=nodeMeta(type);
    return meta?meta->category:"";
}

// Menu order. Hidden legacy nodes are omitted.
inline const std::vector<int>& nodeCatalog() {
    static const std::vector<int> catalog = {
        MidiNode,DawMidiNode,ClipNode,InputNode,DawAudioNode,ChordNode,ProgressNode,ClockNode,DawClockNode,
        Osc1Node,ArpNode,ArrangerNode,VelocityNode,ModelNode,MixNode,EQNode,DelayNode,FilterNode,ReverbNode,NoiseNode,
        StartNode,StopNode,WaitNode,CountNode,
        HardwareOutputNode,DawOutputNode,MidiOutNode
    };
    return catalog;
}

inline const ParamInfo* nodeParamInfo(int p) {
    switch(p) {
        case FilterType:return &filterTypeInfo;
        case FilterCutoff:return &filterCutoffInfo;
        case FilterResonance:return &filterResonanceInfo;
        case FilterDrive:return &filterDriveInfo;
        case ReverbMix:return &reverbMixInfo;
        case ReverbSize:return &reverbSizeInfo;
        case ReverbDamping:return &reverbDampingInfo;
        case ReverbWidth:return &reverbWidthInfo;
        case NoiseLevel:return &noiseLevelInfo;
        case NoiseType:return &noiseTypeInfo;
        default:return nullptr;
    }
}

inline constexpr bool nodeAcceptsParam(int type,int p) {
    if(type==FilterNode) return p>=FilterType && p<=FilterDrive;
    if(type==ReverbNode) return p>=ReverbMix && p<=ReverbWidth;
    if(type==NoiseNode) return p==NoiseLevel || p==NoiseType;
    if(type>=FilterNode) return false;
    return p>=0 && p<ParamCount && !routeParam(p);
}

inline std::vector<int> nodeParamList(int type) {
    switch(type) {
        case Osc1Node:return {Wave1,Level1,Tune1,Attack,Decay,Sustain,Release};
        case ArpNode:return {ArpOn,Pattern,ArpRepeat,Gate};
        case ChordNode:return {ChordRun,ChordRoot,ChordQuality};
        case ProgressNode:return {ProgressRun,ProgressLength,ProgressBars};
        case ClipNode:return {ClipRun,ClipLoop};
        case ClockNode:return {ClockBPM};
        case VelocityNode:return {VelocityMin,VelocityMax,VelocityRate,VelocityShape};
        case EQNode:return {EQLow,EQMid,EQHigh};
        case DelayNode:return {DelayTime,DelayFeedback,DelayMix};
        case OutputNode:case HardwareOutputNode:case DawOutputNode:return {Master};
        case WaitNode:return {WaitSeconds};
        case CountNode:return {TriggerCount};
        case ArrangerNode:return {ArrHighMin,ArrHighMax,ArrHighTexture,ArrMidMin,ArrMidMax,ArrMidTexture,ArrLowMin,ArrLowMax,ArrLowTexture};
        case FilterNode:return {FilterType,FilterCutoff,FilterResonance,FilterDrive};
        case ReverbNode:return {ReverbMix,ReverbSize,ReverbDamping,ReverbWidth};
        case NoiseNode:return {NoiseLevel,NoiseType};
        default:return {};
    }
}
} // namespace tri
