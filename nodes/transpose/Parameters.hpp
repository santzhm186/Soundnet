#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::transpose {
inline constexpr int typeID=nodeTypeID("soundnet.transpose");
inline constexpr int TransposeSemitones=parameterID("soundnet.transpose","transpose.semitones");
inline constexpr ParamInfo TransposeSemitonesInfo{"Semitones",-24.0f,24.0f,0.0f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{TransposeSemitones,ParamSpec{&TransposeSemitonesInfo,"transpose.semitones",true,nullptr,0,ParamGroup(1)}}};}
}
