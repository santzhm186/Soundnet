#pragma once
#include "../../platform/NodeDescriptor.hpp"
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.phaser
// soundnet-order: 29
namespace tri::nodes::phaser {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.phaser",typeID,"相位器","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {PhaserRate,PhaserDepth,PhaserFeedback,PhaserMix}, nullptr};
    spec.kernelFactory=create;
    spec.presets={
            {"慢速扫频",{{PhaserRate,.3f},{PhaserDepth,.8f},{PhaserFeedback,.35f},{PhaserMix,.5f}}},
            {"喷气",{{PhaserRate,1.5f},{PhaserDepth,.6f},{PhaserFeedback,.6f},{PhaserMix,.6f}}} };
    spec.help="Rate LFO 速度，Depth 扫频深度，Feedback 反馈量，Mix 干湿比。";
    return {std::move(spec),parameterSpecs(),{{"音频","Audio"},{"处理","Processing"},{"相位器","Phaser"},{"干湿比","Mix"},{"速率","Rate"},{"深度","Depth"},{"反馈","Feedback"},{"慢速扫频","Slow Sweep"},{"喷气","Jet"},{"Rate LFO 速度，Depth 扫频深度，Feedback 反馈量，Mix 干湿比。","Rate sets LFO speed, Depth the sweep, Feedback the regeneration, and Mix the dry/wet balance."}}};
}
} // namespace tri::nodes::phaser
