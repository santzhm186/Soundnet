#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::arp {
inline constexpr int typeID=nodeTypeID("soundnet.arp");
inline constexpr int ArpOn=parameterID("soundnet.arp","arp.enabled");
inline constexpr ParamInfo ArpOnInfo{"Arpeggiator",0.0f,1.0f,0.0f,""};
inline constexpr const char* ArpOnOptions[]={"关闭","开启"};
inline constexpr int Pattern=parameterID("soundnet.arp","arp.pattern");
inline constexpr ParamInfo PatternInfo{"Rhythm",0.0f,4.0f,0.0f,""};
inline constexpr const char* PatternOptions[]={"直八","Bossa Nova","Reggae","Disco","十六分脉冲"};
inline constexpr int ArpRepeat=parameterID("soundnet.arp","arp.repeat");
inline constexpr ParamInfo ArpRepeatInfo{"Arp Repeat",1.0f,8.0f,1.0f,""};
inline constexpr int Gate=parameterID("soundnet.arp","arp.gate");
inline constexpr ParamInfo GateInfo{"Gate",0.08f,0.98f,0.6f,""};
inline constexpr int ClockBPM=parameterID("soundnet.arp","clock.bpm");
inline constexpr ParamInfo ClockBPMInfo{"Clock BPM",40.0f,240.0f,112.0f,"BPM"};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{ArpOn,ParamSpec{&ArpOnInfo,"arp.enabled",true,ArpOnOptions,2,ParamGroup(3)}},{Pattern,ParamSpec{&PatternInfo,"arp.pattern",true,PatternOptions,5,ParamGroup(4)}},{ArpRepeat,ParamSpec{&ArpRepeatInfo,"arp.repeat",true,nullptr,0,ParamGroup(4)}},{Gate,ParamSpec{&GateInfo,"arp.gate",false,nullptr,0,ParamGroup(4)}},{ClockBPM,ParamSpec{&ClockBPMInfo,"clock.bpm",false,nullptr,0,ParamGroup(4)}}};}
}
