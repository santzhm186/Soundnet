#pragma once
#include "../../platform/NodeDescriptor.hpp"
// soundnet-module: soundnet.gain
// soundnet-order: 100
#include "Kernel.hpp"
namespace tri::nodes::gain {
inline constexpr ParamInfo amountInfo{"Gain",0,4,1,"x"};
inline NodeModule module() {
    using namespace tri::detail;
    NodeSpec spec{"soundnet.gain",nodeTypeID("soundnet.gain"),"增益","声音处理",1,1,true,true,
        CapStream|CapBypassable,TimeMode::Stream,0,
        {pin("audio","音频",Signal::Audio,RoleMain)},
        {pout("audio","音频",Signal::Audio,RoleMain)},{amountID},nullptr};
    spec.kernelFactory=create;
    return {std::move(spec),{{amountID,ParamSpec{&amountInfo,"gain",false,nullptr,0,ParamGroup::Mix}}},
        {{"增益","Gain"},{"处理","Processing"},{"音频","Audio"}}};
}
}
