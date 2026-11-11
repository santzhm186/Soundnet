#pragma once
#include "../../platform/NodeDescriptor.hpp"
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.eq
// soundnet-order: 16
namespace tri::nodes::eq {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.eq",typeID,"EQ","声音处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {EQLow,EQMid,EQHigh}, nullptr};
    spec.kernelFactory=create;
    return {std::move(spec),parameterSpecs(),{{"音频","Audio"},{"处理","Processing"},{"低频","EQ Low"},{"中频","EQ Mid"},{"高频","EQ High"}}};
}
} // namespace tri::nodes::eq
