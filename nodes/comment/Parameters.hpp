#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::comment {
inline constexpr int typeID=nodeTypeID("soundnet.comment");
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {};}
}
