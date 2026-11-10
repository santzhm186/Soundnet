#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::mixer {
inline constexpr int typeID=nodeTypeID("soundnet.mixer");
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {};}
}
