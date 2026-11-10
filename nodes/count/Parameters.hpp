#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::count {
inline constexpr int typeID=nodeTypeID("soundnet.count");
inline constexpr int TriggerCount=parameterID("soundnet.count","trigger.count");
inline constexpr ParamInfo TriggerCountInfo{"Trigger Count",1.0f,1000000.0f,4.0f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{TriggerCount,ParamSpec{&TriggerCountInfo,"trigger.count",true,nullptr,0,ParamGroup(4)}}};}
}
