#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::velocity {
inline constexpr int typeID=nodeTypeID("soundnet.velocity");
inline constexpr int VelocityMin=parameterID("soundnet.velocity","velocity.minimum");
inline constexpr ParamInfo VelocityMinInfo{"Velocity Minimum",1.0f,127.0f,48.0f,""};
inline constexpr int VelocityMax=parameterID("soundnet.velocity","velocity.maximum");
inline constexpr ParamInfo VelocityMaxInfo{"Velocity Maximum",1.0f,127.0f,112.0f,""};
inline constexpr int VelocityRate=parameterID("soundnet.velocity","velocity.beats");
inline constexpr ParamInfo VelocityRateInfo{"Velocity Beats",0.25f,16.0f,4.0f,"beats"};
inline constexpr int VelocityShape=parameterID("soundnet.velocity","velocity.shape");
inline constexpr ParamInfo VelocityShapeInfo{"Velocity Shape",0.0f,2.0f,0.0f,""};
inline constexpr int ClockBPM=parameterID("soundnet.velocity","clock.bpm");
inline constexpr ParamInfo ClockBPMInfo{"Clock BPM",40.0f,240.0f,112.0f,"BPM"};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{VelocityMin,ParamSpec{&VelocityMinInfo,"velocity.minimum",true,nullptr,0,ParamGroup(6)}},{VelocityMax,ParamSpec{&VelocityMaxInfo,"velocity.maximum",true,nullptr,0,ParamGroup(6)}},{VelocityRate,ParamSpec{&VelocityRateInfo,"velocity.beats",false,nullptr,0,ParamGroup(4)}},{VelocityShape,ParamSpec{&VelocityShapeInfo,"velocity.shape",true,nullptr,0,ParamGroup(5)}},{ClockBPM,ParamSpec{&ClockBPMInfo,"clock.bpm",false,nullptr,0,ParamGroup(4)}}};}
}
