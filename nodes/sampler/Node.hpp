#pragma once
#include "../../platform/NodeDescriptor.hpp"
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.sampler
// soundnet-order: 21
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
    NodeSpec spec{"soundnet.sampler",typeID,"采样器","发声",1,1,true,true,
             CapStream|CapBypassable|CapAssetAudio,TimeMode::Stream,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {SamplerRoot,SamplerGain,SamplerLoop,SamplerAttack,SamplerRelease,SamplerStart,
              SamplerEnd,SamplerReverse,SamplerVelocity,SamplerXfade}, "audio"};
    spec.validateConfig=validate;
    spec.kernelFactory=create;
    spec.presets={
            {"平滑循环",{{SamplerLoop,1},{SamplerXfade,20},{SamplerAttack,.005f},{SamplerRelease,.12f}}} };
    spec.importExtensions={"wav","aif","aiff","aifc","mp3","m4a","aac","caf","flac"};
    spec.assetDropPriority=1;
    return {std::move(spec),parameterSpecs(),{{"开始","Start"},{"音频起止位置无效：开始必须早于结束。","Invalid audio region: start must be before end."},{"音频","Audio"},{"音符","Notes"},{"循环  0 / 1","Loop"},{"力度","Velocity"},{"处理","Processing"},{"关闭","Off"},{"开启","On"},{"采样器","Sampler"},{"根音","Root"},{"增益","Gain"},{"起音","Attack"},{"释放","Release"},{"反向","Reverse"},{"循环交叉淡化","Loop XFade"},{"终点","End"},{"平滑循环","Smooth Loop"}}};
}
} // namespace tri::nodes::sampler
