#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::audio_input {
inline constexpr int typeID=nodeTypeID("soundnet.audio-input");
inline constexpr int InputGain=parameterID("soundnet.audio-input","input.gain");
inline constexpr ParamInfo InputGainInfo{"Audio Input",0.0f,1.0f,1.0f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{InputGain,ParamSpec{&InputGainInfo,"input.gain",false,nullptr,0,ParamGroup(6)}}};}
}
