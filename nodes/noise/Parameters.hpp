#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::noise {
inline constexpr int typeID=nodeTypeID("soundnet.noise");
inline constexpr int NoiseLevel=parameterID("soundnet.noise","noise.level");
inline constexpr ParamInfo NoiseLevelInfo{"Level",0.0f,1.0f,0.2f,""};
inline constexpr int NoiseType=parameterID("soundnet.noise","noise.type");
inline constexpr ParamInfo NoiseTypeInfo{"Type",0.0f,1.0f,0.0f,""};
inline constexpr const char* NoiseTypeOptions[]={"白噪声","粉红噪声"};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{NoiseLevel,ParamSpec{&NoiseLevelInfo,"noise.level",false,nullptr,0,ParamGroup(6)}},{NoiseType,ParamSpec{&NoiseTypeInfo,"noise.type",true,NoiseTypeOptions,2,ParamGroup(1)}}};}
}
