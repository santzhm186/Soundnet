#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::daw_midi_input {
inline constexpr int typeID=nodeTypeID("soundnet.daw-midi-input");
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {};}
}
