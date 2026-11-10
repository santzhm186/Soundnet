#pragma once
#include "../../platform/NodeDescriptor.hpp"
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.reverb
// soundnet-order: 19
namespace tri::nodes::reverb {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.reverb",typeID,"混响","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {ReverbMix,ReverbSize,ReverbDamping,ReverbWidth,ReverbPreDelay}, nullptr};
    spec.kernelFactory=create;
    spec.presets={
            {"房间",{{ReverbMix,.25f},{ReverbSize,.55f},{ReverbDamping,.5f},{ReverbWidth,.8f}}},
            {"大厅",{{ReverbMix,.45f},{ReverbSize,.9f},{ReverbDamping,.3f},{ReverbWidth,1}}},
            {"近场预延迟",{{ReverbMix,.3f},{ReverbSize,.4f},{ReverbDamping,.5f},{ReverbWidth,.9f},{ReverbPreDelay,15}}} };
    spec.help="Mix 干湿比，Size 空间大小，Damping 高频吸收，Width 立体声宽度。";
    return {std::move(spec),parameterSpecs(),{{"音频","Audio"},{"处理","Processing"},{"混响","Reverb"},{"干湿比","Mix"},{"空间大小","Size"},{"阻尼","Damping"},{"宽度","Width"},{"预延迟","PreDelay"},{"房间","Room"},{"大厅","Hall"},{"近场预延迟","Near-field Pre-delay"},{"Mix 干湿比，Size 空间大小，Damping 高频吸收，Width 立体声宽度。","Mix sets the dry/wet balance, Size the room size, Damping high-frequency absorption, and Width the stereo spread."}}};
}
} // namespace tri::nodes::reverb
