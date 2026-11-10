#pragma once
#include "../../platform/NodeDescriptor.hpp"
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.bitcrusher
// soundnet-order: 28
namespace tri::nodes::bitcrusher {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.bitcrusher",typeID,"位深破碎","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {CrushBits,CrushRate,CrushMix}, nullptr};
    spec.kernelFactory=create;
    spec.presets={
            {"8-bit",{{CrushBits,8},{CrushRate,12000},{CrushMix,.8f}}},
            {"破碎",{{CrushBits,4},{CrushRate,4000},{CrushMix,1}}} };
    spec.help="Bits 1–16 位深，Rate 500–48000 Hz 采样保持，Mix 干湿比。";
    return {std::move(spec),parameterSpecs(),{{"音频","Audio"},{"处理","Processing"},{"位深破碎","Bitcrusher"},{"干湿比","Mix"},{"速率","Rate"},{"位数","Bits"},{"破碎","Crushed"},{"Bits 1–16 位深，Rate 500–48000 Hz 采样保持，Mix 干湿比。","Bits: 1–16 bit depth. Rate: 500–48000 Hz sample hold. Mix sets dry/wet balance."}}};
}
} // namespace tri::nodes::bitcrusher
