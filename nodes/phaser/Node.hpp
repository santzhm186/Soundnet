#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.phaser
// soundnet-order: 29
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::phaser {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.phaser",PhaserNode,"相位器","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {PhaserRate,PhaserDepth,PhaserFeedback,PhaserMix}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::phaser
