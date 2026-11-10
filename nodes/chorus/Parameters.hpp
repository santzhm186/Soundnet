#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::chorus {
inline constexpr int typeID=nodeTypeID("soundnet.chorus");
inline constexpr int ChorusRate=parameterID("soundnet.chorus","chorus.rate");
inline constexpr ParamInfo ChorusRateInfo{"Rate",0.05f,8.0f,0.6f,"Hz"};
inline constexpr int ChorusDepth=parameterID("soundnet.chorus","chorus.depth");
inline constexpr ParamInfo ChorusDepthInfo{"Depth",0.0f,1.0f,0.35f,""};
inline constexpr int ChorusMix=parameterID("soundnet.chorus","chorus.mix");
inline constexpr ParamInfo ChorusMixInfo{"Mix",0.0f,1.0f,0.35f,""};
inline constexpr int ChorusDelay=parameterID("soundnet.chorus","chorus.delay");
inline constexpr ParamInfo ChorusDelayInfo{"Delay",1.0f,30.0f,12.0f,"ms"};
inline constexpr int ChorusFeedback=parameterID("soundnet.chorus","chorus.feedback");
inline constexpr ParamInfo ChorusFeedbackInfo{"Feedback",0.0f,0.85f,0.0f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{ChorusRate,ParamSpec{&ChorusRateInfo,"chorus.rate",false,nullptr,0,ParamGroup(5)}},{ChorusDepth,ParamSpec{&ChorusDepthInfo,"chorus.depth",false,nullptr,0,ParamGroup(5)}},{ChorusMix,ParamSpec{&ChorusMixInfo,"chorus.mix",false,nullptr,0,ParamGroup(6)}},{ChorusDelay,ParamSpec{&ChorusDelayInfo,"chorus.delay",false,nullptr,0,ParamGroup(5)}},{ChorusFeedback,ParamSpec{&ChorusFeedbackInfo,"chorus.feedback",false,nullptr,0,ParamGroup(5)}}};}
}
