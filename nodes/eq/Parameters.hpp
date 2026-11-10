#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::eq {
inline constexpr int typeID=nodeTypeID("soundnet.eq");
inline constexpr int EQLow=parameterID("soundnet.eq","eq.low");
inline constexpr ParamInfo EQLowInfo{"EQ Low",-12.0f,12.0f,0.0f,"dB"};
inline constexpr int EQMid=parameterID("soundnet.eq","eq.mid");
inline constexpr ParamInfo EQMidInfo{"EQ Mid",-12.0f,12.0f,0.0f,"dB"};
inline constexpr int EQHigh=parameterID("soundnet.eq","eq.high");
inline constexpr ParamInfo EQHighInfo{"EQ High",-12.0f,12.0f,0.0f,"dB"};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{EQLow,ParamSpec{&EQLowInfo,"eq.low",false,nullptr,0,ParamGroup(1)}},{EQMid,ParamSpec{&EQMidInfo,"eq.mid",false,nullptr,0,ParamGroup(1)}},{EQHigh,ParamSpec{&EQHighInfo,"eq.high",false,nullptr,0,ParamGroup(1)}}};}
}
