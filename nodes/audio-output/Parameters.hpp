#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::audio_output {
inline constexpr int typeID=nodeTypeID("soundnet.audio-output");
inline constexpr int Master=parameterID("soundnet.audio-output","output.master");
inline constexpr ParamInfo MasterInfo{"Master",0.0f,1.0f,0.74f,""};
inline constexpr int OutputWidth=parameterID("soundnet.audio-output","output.width");
inline constexpr ParamInfo OutputWidthInfo{"Width",0.0f,2.0f,1.0f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{Master,ParamSpec{&MasterInfo,"output.master",false,nullptr,0,ParamGroup(6)}},{OutputWidth,ParamSpec{&OutputWidthInfo,"output.width",false,nullptr,0,ParamGroup(6)}}};}
}
