#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.compressor
// soundnet-order: 23
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::compressor {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.compressor",CompressorNode,"压缩器","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req),pin("sidechain","侧链",Signal::Audio,RoleSidechain)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {CompThreshold,CompRatio,CompAttack,CompRelease,CompMakeup,CompMix}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::compressor
