#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::chord {
inline constexpr int typeID=nodeTypeID("soundnet.chord");
inline constexpr int ChordRun=parameterID("soundnet.chord","chord.run");
inline constexpr ParamInfo ChordRunInfo{"Chord Play",0.0f,1.0f,0.0f,""};
inline constexpr const char* ChordRunOptions[]={"关闭","开启"};
inline constexpr int ChordRoot=parameterID("soundnet.chord","chord.root");
inline constexpr ParamInfo ChordRootInfo{"Chord Root",24.0f,96.0f,60.0f,"MIDI"};
inline constexpr int ChordQuality=parameterID("soundnet.chord","chord.quality");
inline constexpr ParamInfo ChordQualityInfo{"Chord Quality",0.0f,7.0f,3.0f,""};
inline constexpr const char* ChordQualityOptions[]={"大三和弦","小三和弦","属七","大七","小七","减三","挂二","挂四"};
inline constexpr int ClockBPM=parameterID("soundnet.chord","clock.bpm");
inline constexpr ParamInfo ClockBPMInfo{"Clock BPM",40.0f,240.0f,112.0f,"BPM"};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{ChordRun,ParamSpec{&ChordRunInfo,"chord.run",true,ChordRunOptions,2,ParamGroup(3)}},{ChordRoot,ParamSpec{&ChordRootInfo,"chord.root",true,nullptr,0,ParamGroup(8)}},{ChordQuality,ParamSpec{&ChordQualityInfo,"chord.quality",true,ChordQualityOptions,8,ParamGroup(8)}},{ClockBPM,ParamSpec{&ClockBPMInfo,"clock.bpm",false,nullptr,0,ParamGroup(4)}}};}
}
