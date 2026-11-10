#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::note_model {
inline constexpr int typeID=nodeTypeID("soundnet.note-model");
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {};}
}
