#pragma once
#include "../../platform/NodeDescriptor.hpp"
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.compressor
// soundnet-order: 23
namespace tri::nodes::compressor {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.compressor",typeID,"压缩器","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req),pin("sidechain","侧链",Signal::Audio,RoleSidechain)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {CompThreshold,CompRatio,CompAttack,CompRelease,CompMakeup,CompMix}, nullptr};
    spec.kernelFactory=create;
    spec.presets={
            {"轻压缩",{{CompThreshold,-18},{CompRatio,2},{CompAttack,.01f},{CompRelease,.15f},{CompMakeup,1.1f},{CompMix,1}}},
            {"鼓总线",{{CompThreshold,-24},{CompRatio,4},{CompAttack,.005f},{CompRelease,.12f},{CompMakeup,1.4f},{CompMix,1}}},
            {"限制",{{CompThreshold,-6},{CompRatio,20},{CompAttack,.001f},{CompRelease,.08f},{CompMakeup,1},{CompMix,1}}} };
    spec.help="Threshold 阈值，Ratio 压缩比，Attack/Release 包络，Makeup 补偿增益，Mix 干湿比。第二个 Audio 输入为侧链检测信号，可做闪避。";
    return {std::move(spec),parameterSpecs(),{{"音频","Audio"},{"处理","Processing"},{"压缩器","Compressor"},{"侧链","Sidechain"},{"干湿比","Mix"},{"起音","Attack"},{"释放","Release"},{"阈值","Threshold"},{"压缩比","Ratio"},{"补偿增益","Makeup"},{"轻压缩","Gentle Compression"},{"鼓总线","Drum Bus"},{"限制","Limiting"},{"Threshold 阈值，Ratio 压缩比，Attack/Release 包络，Makeup 补偿增益，Mix 干湿比。第二个 Audio 输入为侧链检测信号，可做闪避。","Threshold and Ratio control compression; Attack/Release set its envelope. Makeup restores gain; Mix blends dry/wet. The second audio input accepts a sidechain for ducking."}}};
}
} // namespace tri::nodes::compressor
