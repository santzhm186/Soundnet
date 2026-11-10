#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::compressor {
inline constexpr int typeID=nodeTypeID("soundnet.compressor");
inline constexpr int CompThreshold=parameterID("soundnet.compressor","compressor.threshold");
inline constexpr ParamInfo CompThresholdInfo{"Threshold",-60.0f,0.0f,-18.0f,"dB"};
inline constexpr int CompRatio=parameterID("soundnet.compressor","compressor.ratio");
inline constexpr ParamInfo CompRatioInfo{"Ratio",1.0f,20.0f,4.0f,":1"};
inline constexpr int CompAttack=parameterID("soundnet.compressor","compressor.attack");
inline constexpr ParamInfo CompAttackInfo{"Attack",0.001f,1.0f,0.01f,"s"};
inline constexpr int CompRelease=parameterID("soundnet.compressor","compressor.release");
inline constexpr ParamInfo CompReleaseInfo{"Release",0.01f,2.0f,0.15f,"s"};
inline constexpr int CompMakeup=parameterID("soundnet.compressor","compressor.makeup");
inline constexpr ParamInfo CompMakeupInfo{"Makeup",0.0f,2.0f,1.0f,""};
inline constexpr int CompMix=parameterID("soundnet.compressor","compressor.mix");
inline constexpr ParamInfo CompMixInfo{"Mix",0.0f,1.0f,1.0f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{CompThreshold,ParamSpec{&CompThresholdInfo,"compressor.threshold",false,nullptr,0,ParamGroup(7)}},{CompRatio,ParamSpec{&CompRatioInfo,"compressor.ratio",false,nullptr,0,ParamGroup(7)}},{CompAttack,ParamSpec{&CompAttackInfo,"compressor.attack",false,nullptr,0,ParamGroup(7)}},{CompRelease,ParamSpec{&CompReleaseInfo,"compressor.release",false,nullptr,0,ParamGroup(7)}},{CompMakeup,ParamSpec{&CompMakeupInfo,"compressor.makeup",false,nullptr,0,ParamGroup(6)}},{CompMix,ParamSpec{&CompMixInfo,"compressor.mix",false,nullptr,0,ParamGroup(6)}}};}
}
