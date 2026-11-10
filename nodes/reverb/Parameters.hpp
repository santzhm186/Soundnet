#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::reverb {
inline constexpr int typeID=nodeTypeID("soundnet.reverb");
inline constexpr int ReverbMix=parameterID("soundnet.reverb","reverb.mix");
inline constexpr ParamInfo ReverbMixInfo{"Mix",0.0f,1.0f,0.25f,""};
inline constexpr int ReverbSize=parameterID("soundnet.reverb","reverb.size");
inline constexpr ParamInfo ReverbSizeInfo{"Size",0.0f,1.0f,0.55f,""};
inline constexpr int ReverbDamping=parameterID("soundnet.reverb","reverb.damping");
inline constexpr ParamInfo ReverbDampingInfo{"Damping",0.0f,1.0f,0.4f,""};
inline constexpr int ReverbWidth=parameterID("soundnet.reverb","reverb.width");
inline constexpr ParamInfo ReverbWidthInfo{"Width",0.0f,1.0f,0.9f,""};
inline constexpr int ReverbPreDelay=parameterID("soundnet.reverb","reverb.preDelay");
inline constexpr ParamInfo ReverbPreDelayInfo{"PreDelay",0.0f,200.0f,0.0f,"ms"};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{ReverbMix,ParamSpec{&ReverbMixInfo,"reverb.mix",false,nullptr,0,ParamGroup(6)}},{ReverbSize,ParamSpec{&ReverbSizeInfo,"reverb.size",false,nullptr,0,ParamGroup(5)}},{ReverbDamping,ParamSpec{&ReverbDampingInfo,"reverb.damping",false,nullptr,0,ParamGroup(5)}},{ReverbWidth,ParamSpec{&ReverbWidthInfo,"reverb.width",false,nullptr,0,ParamGroup(5)}},{ReverbPreDelay,ParamSpec{&ReverbPreDelayInfo,"reverb.preDelay",false,nullptr,0,ParamGroup(5)}}};}
}
