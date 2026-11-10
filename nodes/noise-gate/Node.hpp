#pragma once
#include "../../platform/NodeDescriptor.hpp"
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.noise-gate
// soundnet-order: 32
namespace tri::nodes::noise_gate {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.noise-gate",typeID,"噪声门","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {GateThreshold,GateAttack,GateRelease,GateRange}, nullptr};
    spec.kernelFactory=create;
    spec.presets={
            {"轻门",{{GateThreshold,-50},{GateAttack,.002f},{GateRelease,.25f},{GateRange,.6f}}},
            {"硬门",{{GateThreshold,-24},{GateAttack,.001f},{GateRelease,.05f},{GateRange,1}}} };
    spec.help="低于 Threshold 的音频会被压低；Range 控制关闭时的衰减深度，Attack/Release 控制开合速度。";
    return {std::move(spec),parameterSpecs(),{{"音频","Audio"},{"处理","Processing"},{"噪声门","Noise Gate"},{"起音","Attack"},{"释放","Release"},{"阈值","Threshold"},{"范围","Range"},{"轻门","Soft Gate"},{"硬门","Hard Gate"},{"低于 Threshold 的音频会被压低；Range 控制关闭时的衰减深度，Attack/Release 控制开合速度。","Attenuates audio below Threshold. Range sets closed-gate attenuation; Attack and Release control opening and closing."}}};
}
} // namespace tri::nodes::noise_gate
