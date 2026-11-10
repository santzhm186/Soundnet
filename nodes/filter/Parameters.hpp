#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::filter {
inline constexpr int typeID=nodeTypeID("soundnet.filter");
inline constexpr int FilterType=parameterID("soundnet.filter","filter.type");
inline constexpr ParamInfo FilterTypeInfo{"Type",0.0f,2.0f,0.0f,""};
inline constexpr const char* FilterTypeOptions[]={"低通","带通","高通"};
inline constexpr int FilterCutoff=parameterID("soundnet.filter","filter.cutoff");
inline constexpr ParamInfo FilterCutoffInfo{"Cutoff",20.0f,20000.0f,8000.0f,"Hz"};
inline constexpr int FilterResonance=parameterID("soundnet.filter","filter.resonance");
inline constexpr ParamInfo FilterResonanceInfo{"Resonance",0.5f,20.0f,0.9f,"Q"};
inline constexpr int FilterDrive=parameterID("soundnet.filter","filter.drive");
inline constexpr ParamInfo FilterDriveInfo{"Drive",0.0f,1.0f,0.0f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{FilterType,ParamSpec{&FilterTypeInfo,"filter.type",true,FilterTypeOptions,3,ParamGroup(1)}},{FilterCutoff,ParamSpec{&FilterCutoffInfo,"filter.cutoff",false,nullptr,0,ParamGroup(1)}},{FilterResonance,ParamSpec{&FilterResonanceInfo,"filter.resonance",false,nullptr,0,ParamGroup(1)}},{FilterDrive,ParamSpec{&FilterDriveInfo,"filter.drive",false,nullptr,0,ParamGroup(1)}}};}
}
