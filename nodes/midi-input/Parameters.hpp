#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::midi_input {
inline constexpr int typeID=nodeTypeID("soundnet.midi-input");
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {};}
}
