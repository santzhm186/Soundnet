#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::midi_clip {
inline constexpr int typeID=nodeTypeID("soundnet.midi-clip");
inline constexpr int ClipRun=parameterID("soundnet.midi-clip","clip.run");
inline constexpr ParamInfo ClipRunInfo{"Clip Play",0.0f,1.0f,0.0f,""};
inline constexpr const char* ClipRunOptions[]={"关闭","开启"};
inline constexpr int ClipLoop=parameterID("soundnet.midi-clip","clip.loop");
inline constexpr ParamInfo ClipLoopInfo{"Clip Loop",0.0f,1.0f,1.0f,""};
inline constexpr const char* ClipLoopOptions[]={"关闭","开启"};
inline constexpr int ClockBPM=parameterID("soundnet.midi-clip","clock.bpm");
inline constexpr ParamInfo ClockBPMInfo{"Clock BPM",40.0f,240.0f,112.0f,"BPM"};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{ClipRun,ParamSpec{&ClipRunInfo,"clip.run",true,ClipRunOptions,2,ParamGroup(3)}},{ClipLoop,ParamSpec{&ClipLoopInfo,"clip.loop",true,ClipLoopOptions,2,ParamGroup(3)}},{ClockBPM,ParamSpec{&ClockBPMInfo,"clock.bpm",false,nullptr,0,ParamGroup(4)}}};}
}
