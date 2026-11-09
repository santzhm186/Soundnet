#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.sampler
// soundnet-order: 21
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::sampler {
inline bool validate(const KernelConfig& config,std::string& why) {
    if(config.value(SamplerStart,0)>=config.value(SamplerEnd,1)) {
        why="音频起止位置无效：开始必须早于结束。";return false;
    }
    return true;
}
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.sampler",SamplerNode,"采样器","处理",1,1,true,true,
             CapStream|CapBypassable|CapAssetAudio,TimeMode::Stream,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {SamplerRoot,SamplerGain,SamplerLoop,SamplerAttack,SamplerRelease,SamplerStart,
              SamplerEnd,SamplerReverse,SamplerVelocity,SamplerXfade}, "audio"};
    spec.validateConfig=validate;
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::sampler
