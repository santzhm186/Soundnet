#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::stop {
inline constexpr int typeID=nodeTypeID("soundnet.stop");
inline constexpr int TriggerPulse=parameterID("soundnet.stop","trigger.pulse");
inline constexpr ParamInfo TriggerPulseInfo{"Trigger Pulse",0.0f,1000000.0f,0.0f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{TriggerPulse,ParamSpec{&TriggerPulseInfo,"trigger.pulse",true,nullptr,0,ParamGroup(0),true}}};}
}
