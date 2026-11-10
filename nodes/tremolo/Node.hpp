#pragma once
#include "../../platform/NodeDescriptor.hpp"
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.tremolo
// soundnet-order: 27
namespace tri::nodes::tremolo {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.tremolo",typeID,"颤音","处理",1,1,true,true,
             CapStream|CapBypassable|CapTemporal,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {TremoloRate,TremoloDepth,TremoloMix,TremoloStereo,TremoloSync,TremoloDivision,ClockBPM}, nullptr};
    spec.kernelFactory=create;
    spec.presets={
            {"慢速",{{TremoloRate,2},{TremoloDepth,.4f},{TremoloMix,.8f},{TremoloStereo,0}}},
            {"快速",{{TremoloRate,8},{TremoloDepth,.6f},{TremoloMix,1},{TremoloStereo,1}}},
            {"同步 1/8",{{TremoloSync,1},{TremoloDivision,1},{TremoloDepth,.6f},{TremoloMix,1}}} };
    spec.help="Rate 0.1–20 Hz，Depth 0–1，Mix 干湿比，Stereo 左右相位差。";
    return {std::move(spec),parameterSpecs(),{{"音频","Audio"},{"立体声","Stereo"},{"处理","Processing"},{"关闭","Off"},{"开启","On"},{"四分","Quarter Note"},{"八分","Eighth Note"},{"十六分","Sixteenth Note"},{"附点四分","Dotted Quarter"},{"颤音","Tremolo"},{"干湿比","Mix"},{"速率","Rate"},{"深度","Depth"},{"同步","Sync"},{"节拍分割","Division"},{"时钟速度","Clock BPM"},{"慢速","Slow"},{"快速","Fast"},{"同步 1/8","Sync 1/8"},{"Rate 0.1–20 Hz，Depth 0–1，Mix 干湿比，Stereo 左右相位差。","Rate: 0.1–20 Hz. Depth: 0–1. Mix sets dry/wet balance; Stereo offsets left/right phase."}}};
}
} // namespace tri::nodes::tremolo
