#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::random {
inline constexpr int typeID=nodeTypeID("soundnet.random");
inline constexpr int RandomChance=parameterID("soundnet.random","random.chance");
inline constexpr ParamInfo RandomChanceInfo{"Chance",0.0f,1.0f,0.5f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{RandomChance,ParamSpec{&RandomChanceInfo,"random.chance",false,nullptr,0,ParamGroup(0)}}};}
}
