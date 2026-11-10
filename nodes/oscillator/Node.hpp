#pragma once
#include "../../platform/NodeDescriptor.hpp"
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.oscillator
// soundnet-order: 10
namespace tri::nodes::oscillator {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.oscillator",typeID,"振荡器","处理",1,1,true,true,
             CapStream,TimeMode::Stream,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {Wave1,Level1,Tune1,Attack,Decay,Sustain,Release}, nullptr};
    spec.kernelFactory=create;
    spec.help="Wave  0 正弦  1 方波  2 锯齿  3 三角";
    return {std::move(spec),parameterSpecs(),{{"音频","Audio"},{"音符","Notes"},{"处理","Processing"},{"正弦","Sine"},{"方波","Square"},{"锯齿","Saw"},{"三角","Triangle"},{"振荡器","Oscillator"},{"起音","Attack"},{"衰减","Decay"},{"延音","Sustain"},{"释放","Release"},{"振荡器 1 波形","Osc 1 Wave"},{"振荡器 1 电平","Osc 1 Level"},{"振荡器 1 调音","Osc 1 Tune"},{"Wave  0 正弦  1 方波  2 锯齿  3 三角","Wave: 0 Sine · 1 Square · 2 Saw · 3 Triangle"}}};
}
} // namespace tri::nodes::oscillator
