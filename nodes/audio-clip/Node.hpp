#pragma once
#include "../../platform/NodeDescriptor.hpp"
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.audio-clip
// soundnet-order: 22
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
    NodeSpec spec{"soundnet.audio-clip",typeID,"音频片段","发声",1,1,true,true,
             CapStream|CapBypassable|CapAssetAudio,TimeMode::Timed,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional|PortFeedback)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {AudioClipGain,AudioClipLoop,AudioClipStart,AudioClipEnd,AudioClipFade,
              AudioClipPitch,AudioClipReverse,AudioClipXfade}, "audio"};
    spec.validateConfig=validate;
    spec.kernelFactory=create;
    spec.presets={
            {"平滑循环",{{AudioClipLoop,1},{AudioClipXfade,20},{AudioClipFade,.005f}}},
            {"反向片段",{{AudioClipLoop,1},{AudioClipReverse,1},{AudioClipXfade,20}}} };
    spec.importExtensions={"wav","aif","aiff","aifc","mp3","m4a","aac","caf","flac"};
    spec.assetDropPriority=2;
    spec.importParameters={{AudioClipLoop,1}};
    return {std::move(spec),parameterSpecs(),{{"开始","Start"},{"音频起止位置无效：开始必须早于结束。","Invalid audio region: start must be before end."},{"音频","Audio"},{"触发","Trigger"},{"循环  0 / 1","Loop"},{"处理","Processing"},{"关闭","Off"},{"开启","On"},{"音频片段","Audio Clip"},{"增益","Gain"},{"反向","Reverse"},{"循环交叉淡化","Loop XFade"},{"终点","End"},{"淡入淡出","Fade"},{"音高","Pitch"},{"平滑循环","Smooth Loop"},{"反向片段","Reversed Clip"}}};
}
} // namespace tri::nodes::audio_clip
