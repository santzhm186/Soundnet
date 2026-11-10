#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::noise_gate {
inline constexpr int typeID=nodeTypeID("soundnet.noise-gate");
inline constexpr int GateThreshold=parameterID("soundnet.noise-gate","gate.threshold");
inline constexpr ParamInfo GateThresholdInfo{"Threshold",-80.0f,0.0f,-40.0f,"dB"};
inline constexpr int GateAttack=parameterID("soundnet.noise-gate","gate.attack");
inline constexpr ParamInfo GateAttackInfo{"Attack",0.001f,1.0f,0.005f,"s"};
inline constexpr int GateRelease=parameterID("soundnet.noise-gate","gate.release");
inline constexpr ParamInfo GateReleaseInfo{"Release",0.01f,2.0f,0.15f,"s"};
inline constexpr int GateRange=parameterID("soundnet.noise-gate","gate.range");
inline constexpr ParamInfo GateRangeInfo{"Range",0.0f,1.0f,0.8f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{GateThreshold,ParamSpec{&GateThresholdInfo,"gate.threshold",false,nullptr,0,ParamGroup(7)}},{GateAttack,ParamSpec{&GateAttackInfo,"gate.attack",false,nullptr,0,ParamGroup(7)}},{GateRelease,ParamSpec{&GateReleaseInfo,"gate.release",false,nullptr,0,ParamGroup(7)}},{GateRange,ParamSpec{&GateRangeInfo,"gate.range",false,nullptr,0,ParamGroup(7)}}};}
}
