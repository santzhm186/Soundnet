#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::wait {
inline constexpr int typeID=nodeTypeID("soundnet.wait");
inline constexpr int WaitSeconds=parameterID("soundnet.wait","wait.seconds");
inline constexpr ParamInfo WaitSecondsInfo{"Wait Seconds",0.0f,3600.0f,1.0f,"s"};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{WaitSeconds,ParamSpec{&WaitSecondsInfo,"wait.seconds",false,nullptr,0,ParamGroup(4)}}};}
}
