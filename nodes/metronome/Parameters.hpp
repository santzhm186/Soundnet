#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::metronome {
inline constexpr int typeID=nodeTypeID("soundnet.metronome");
inline constexpr int MetronomeBPM=parameterID("soundnet.metronome","metronome.bpm");
inline constexpr ParamInfo MetronomeBPMInfo{"BPM",40.0f,240.0f,120.0f,""};
inline constexpr int MetronomeLevel=parameterID("soundnet.metronome","metronome.level");
inline constexpr ParamInfo MetronomeLevelInfo{"Level",0.0f,1.0f,0.4f,""};
inline constexpr int MetronomeAccent=parameterID("soundnet.metronome","metronome.accent");
inline constexpr ParamInfo MetronomeAccentInfo{"Accent",0.0f,1.0f,1.0f,""};
inline constexpr int MetronomeBeats=parameterID("soundnet.metronome","metronome.beats");
inline constexpr ParamInfo MetronomeBeatsInfo{"Beats",1.0f,16.0f,4.0f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{MetronomeBPM,ParamSpec{&MetronomeBPMInfo,"metronome.bpm",false,nullptr,0,ParamGroup(4)}},{MetronomeLevel,ParamSpec{&MetronomeLevelInfo,"metronome.level",false,nullptr,0,ParamGroup(6)}},{MetronomeAccent,ParamSpec{&MetronomeAccentInfo,"metronome.accent",false,nullptr,0,ParamGroup(6)}},{MetronomeBeats,ParamSpec{&MetronomeBeatsInfo,"metronome.beats",true,nullptr,0,ParamGroup(4)}}};}
}
