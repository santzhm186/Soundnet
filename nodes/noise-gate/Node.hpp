#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.noise-gate
// soundnet-order: 32
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::noise_gate {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.noise-gate",GateNode,"噪声门","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {GateThreshold,GateAttack,GateRelease,GateRange}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::noise_gate
