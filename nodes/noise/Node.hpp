#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.noise
// soundnet-order: 20
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::noise {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.noise",NoiseNode,"噪声","处理",1,1,true,true,
             CapStream|CapGenerator|CapBypassable,TimeMode::None,0,
             {}, {pout("audio","音频",Signal::Audio,RoleMain)},
             {NoiseLevel,NoiseType}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::noise
