#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::start {
inline constexpr int typeID=nodeTypeID("soundnet.start");
inline constexpr int TriggerPulse=parameterID("soundnet.start","trigger.pulse");
inline constexpr ParamInfo TriggerPulseInfo{"Trigger Pulse",0.0f,1000000.0f,0.0f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{TriggerPulse,ParamSpec{&TriggerPulseInfo,"trigger.pulse",true,nullptr,0,ParamGroup(0),true}}};}
}
