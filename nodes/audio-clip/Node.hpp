#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.audio-clip
// soundnet-order: 22
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::audio_clip {
inline bool validate(const KernelConfig& config,std::string& why) {
    if(config.value(AudioClipStart,0)>=config.value(AudioClipEnd,1)) {
        why="音频起止位置无效：开始必须早于结束。";return false;
    }
    return true;
}
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.audio-clip",AudioClipNode,"音频片段","处理",1,1,true,true,
             CapStream|CapBypassable|CapAssetAudio,TimeMode::Timed,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional|PortFeedback)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {AudioClipGain,AudioClipLoop,AudioClipStart,AudioClipEnd,AudioClipFade,
              AudioClipPitch,AudioClipReverse,AudioClipXfade}, "audio"};
    spec.validateConfig=validate;
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::audio_clip
