#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.reverb
// soundnet-order: 19
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::reverb {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.reverb",ReverbNode,"混响","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {ReverbMix,ReverbSize,ReverbDamping,ReverbWidth,ReverbPreDelay}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::reverb
