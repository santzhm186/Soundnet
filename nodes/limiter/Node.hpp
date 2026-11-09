#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.limiter
// soundnet-order: 24
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::limiter {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.limiter",LimiterNode,"限幅器","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {LimiterThreshold,LimiterRelease,LimiterCeiling,LimiterGain}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::limiter
