#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::delay {
inline constexpr int typeID=nodeTypeID("soundnet.delay");
inline constexpr int DelayTime=parameterID("soundnet.delay","delay.time");
inline constexpr ParamInfo DelayTimeInfo{"Delay Time",20.0f,1000.0f,310.0f,"ms"};
inline constexpr int DelayFeedback=parameterID("soundnet.delay","delay.feedback");
inline constexpr ParamInfo DelayFeedbackInfo{"Delay Feedback",0.0f,0.88f,0.32f,""};
inline constexpr int DelayMix=parameterID("soundnet.delay","delay.mix");
inline constexpr ParamInfo DelayMixInfo{"Delay Mix",0.0f,1.0f,0.12f,""};
inline constexpr int DelaySync=parameterID("soundnet.delay","delay.sync");
inline constexpr ParamInfo DelaySyncInfo{"Sync",0.0f,1.0f,0.0f,""};
inline constexpr const char* DelaySyncOptions[]={"关闭","开启"};
inline constexpr int DelayDivision=parameterID("soundnet.delay","delay.division");
inline constexpr ParamInfo DelayDivisionInfo{"Division",0.0f,3.0f,1.0f,""};
inline constexpr const char* DelayDivisionOptions[]={"四分","八分","十六分","附点八分"};
inline constexpr int ClockBPM=parameterID("soundnet.delay","clock.bpm");
inline constexpr ParamInfo ClockBPMInfo{"Clock BPM",40.0f,240.0f,112.0f,"BPM"};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{DelayTime,ParamSpec{&DelayTimeInfo,"delay.time",false,nullptr,0,ParamGroup(4)}},{DelayFeedback,ParamSpec{&DelayFeedbackInfo,"delay.feedback",false,nullptr,0,ParamGroup(5)}},{DelayMix,ParamSpec{&DelayMixInfo,"delay.mix",false,nullptr,0,ParamGroup(6)}},{DelaySync,ParamSpec{&DelaySyncInfo,"delay.sync",true,DelaySyncOptions,2,ParamGroup(4)}},{DelayDivision,ParamSpec{&DelayDivisionInfo,"delay.division",true,DelayDivisionOptions,4,ParamGroup(4)}},{ClockBPM,ParamSpec{&ClockBPMInfo,"clock.bpm",false,nullptr,0,ParamGroup(4)}}};}
}
