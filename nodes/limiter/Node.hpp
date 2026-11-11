#pragma once
#include "../../platform/NodeDescriptor.hpp"
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.limiter
// soundnet-order: 24
namespace tri::nodes::limiter {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.limiter",typeID,"限幅器","声音处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {LimiterThreshold,LimiterRelease,LimiterCeiling,LimiterGain}, nullptr};
    spec.kernelFactory=create;
    spec.presets={
            {"轻柔",{{LimiterThreshold,-6},{LimiterRelease,.12f},{LimiterCeiling,-.3f},{LimiterGain,1}}},
            {"砖墙",{{LimiterThreshold,-3},{LimiterRelease,.05f},{LimiterCeiling,-.1f},{LimiterGain,1}}} };
    spec.help="Threshold 开始限幅，Ceiling 输出上限，Release 恢复速度，Gain 输入增益。";
    return {std::move(spec),parameterSpecs(),{{"音频","Audio"},{"处理","Processing"},{"限幅器","Limiter"},{"增益","Gain"},{"释放","Release"},{"阈值","Threshold"},{"输出上限","Ceiling"},{"轻柔","Gentle"},{"砖墙","Brickwall"},{"Threshold 开始限幅，Ceiling 输出上限，Release 恢复速度，Gain 输入增益。","Threshold starts limiting, Ceiling caps output, Release sets recovery, and Gain boosts input."}}};
}
} // namespace tri::nodes::limiter
