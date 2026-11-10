#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::bitcrusher {
inline constexpr int typeID=nodeTypeID("soundnet.bitcrusher");
inline constexpr int CrushBits=parameterID("soundnet.bitcrusher","crush.bits");
inline constexpr ParamInfo CrushBitsInfo{"Bits",1.0f,16.0f,8.0f,"bit"};
inline constexpr int CrushRate=parameterID("soundnet.bitcrusher","crush.rate");
inline constexpr ParamInfo CrushRateInfo{"Rate",500.0f,48000.0f,8000.0f,"Hz"};
inline constexpr int CrushMix=parameterID("soundnet.bitcrusher","crush.mix");
inline constexpr ParamInfo CrushMixInfo{"Mix",0.0f,1.0f,1.0f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{CrushBits,ParamSpec{&CrushBitsInfo,"crush.bits",true,nullptr,0,ParamGroup(1)}},{CrushRate,ParamSpec{&CrushRateInfo,"crush.rate",false,nullptr,0,ParamGroup(1)}},{CrushMix,ParamSpec{&CrushMixInfo,"crush.mix",false,nullptr,0,ParamGroup(6)}}};}
}
