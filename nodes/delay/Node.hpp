#pragma once
#include "../../platform/NodeDescriptor.hpp"
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.delay
// soundnet-order: 17
namespace tri::nodes::delay {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.delay",typeID,"Delay","处理",1,1,true,true,
             CapStream|CapBypassable|CapTemporal,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {DelayTime,DelayFeedback,DelayMix,DelaySync,DelayDivision,ClockBPM}, nullptr};
    spec.kernelFactory=create;
    spec.presets={
            {"短延迟",{{DelayTime,140},{DelayFeedback,.2f},{DelayMix,.15f}}},
            {"长回声",{{DelayTime,420},{DelayFeedback,.45f},{DelayMix,.25f}}} };
    return {std::move(spec),parameterSpecs(),{{"音频","Audio"},{"处理","Processing"},{"关闭","Off"},{"开启","On"},{"四分","Quarter Note"},{"八分","Eighth Note"},{"十六分","Sixteenth Note"},{"附点八分","Dotted Eighth"},{"延迟","Delay"},{"同步","Sync"},{"节拍分割","Division"},{"延迟时间","Delay Time"},{"延迟反馈","Delay Feedback"},{"延迟干湿比","Delay Mix"},{"时钟速度","Clock BPM"},{"短延迟","Short Delay"},{"长回声","Long Echo"}}};
}
} // namespace tri::nodes::delay
