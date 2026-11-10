#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::limiter {
inline constexpr int typeID=nodeTypeID("soundnet.limiter");
inline constexpr int LimiterThreshold=parameterID("soundnet.limiter","limiter.threshold");
inline constexpr ParamInfo LimiterThresholdInfo{"Threshold",-60.0f,0.0f,-6.0f,"dB"};
inline constexpr int LimiterRelease=parameterID("soundnet.limiter","limiter.release");
inline constexpr ParamInfo LimiterReleaseInfo{"Release",0.01f,2.0f,0.12f,"s"};
inline constexpr int LimiterCeiling=parameterID("soundnet.limiter","limiter.ceiling");
inline constexpr ParamInfo LimiterCeilingInfo{"Ceiling",-12.0f,0.0f,-0.3f,"dB"};
inline constexpr int LimiterGain=parameterID("soundnet.limiter","limiter.gain");
inline constexpr ParamInfo LimiterGainInfo{"Gain",0.0f,2.0f,1.0f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{LimiterThreshold,ParamSpec{&LimiterThresholdInfo,"limiter.threshold",false,nullptr,0,ParamGroup(7)}},{LimiterRelease,ParamSpec{&LimiterReleaseInfo,"limiter.release",false,nullptr,0,ParamGroup(7)}},{LimiterCeiling,ParamSpec{&LimiterCeilingInfo,"limiter.ceiling",false,nullptr,0,ParamGroup(7)}},{LimiterGain,ParamSpec{&LimiterGainInfo,"limiter.gain",false,nullptr,0,ParamGroup(6)}}};}
}
