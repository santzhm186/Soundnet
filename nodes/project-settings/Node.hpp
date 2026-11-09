#pragma once
#include "../../platform/NodeDescriptor.hpp"
// soundnet-module: soundnet.project-settings
// soundnet-order: 99
#include "Kernel.hpp"
namespace tri::nodes::project_settings {
inline constexpr int rateID=parameterID("soundnet.project-settings","project.sampleRate");
inline constexpr int bitsID=parameterID("soundnet.project-settings","project.exportBits");
inline constexpr int precisionID=parameterID("soundnet.project-settings","project.processingPrecision");
inline constexpr ParamInfo rateInfo{"Sample rate",8000,192000,48000,"Hz"};
inline constexpr ParamInfo bitsInfo{"Export bit depth",16,32,24,"bit"};
inline constexpr ParamInfo precisionInfo{"Processing precision",32,32,32,"bit float"};
inline bool validate(const KernelConfig& config,std::string& why) {
    const float bits=config.value(bitsID,24);
    if(bits!=16&&bits!=24&&bits!=32) {why="导出位深必须为 16、24 或 32。";return false;}
    return true;
}
inline NodeModule module() {
    NodeSpec spec{"soundnet.project-settings",nodeTypeID("soundnet.project-settings"),"项目设定","工程",1,1,true,true,
        0,TimeMode::None,0,{},{},{rateID,bitsID,precisionID},nullptr,
        {{"project.inputDevice","输入设备","system",256,false,false,true},
         {"project.outputDevice","输出设备","system",256,false,false,true}}};
    spec.kernelFactory=create;spec.validateConfig=validate;
    spec.environmentRole=EnvironmentRole::ProjectSettings;
    return {std::move(spec),{
        {rateID,ParamSpec{&rateInfo,"project.sampleRate",true}},
        {bitsID,ParamSpec{&bitsInfo,"project.exportBits",true}},
        {precisionID,ParamSpec{&precisionInfo,"project.processingPrecision",true}}},
        {{"项目设定","Project Settings"},{"工程","Project"},{"项目采样率","Sample rate"},
         {"导出位深","Export bit depth"},{"处理精度","Processing precision"},
         {"输入设备","Input Device"},{"输出设备","Output Device"},
         {"跟随系统默认","Follow System Default"},{"设备不可用","Device Unavailable"},
         {"音频设备与采样率由宿主管理","Audio devices and sample rate are managed by the host"},
         {"处理","Processing"},{"设备","Device"}}};
}
}
