#pragma once
#include "../../platform/NodeDescriptor.hpp"
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.chorus
// soundnet-order: 25
namespace tri::nodes::chorus {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.chorus",typeID,"合唱","声音处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {ChorusRate,ChorusDepth,ChorusMix,ChorusDelay,ChorusFeedback}, nullptr};
    spec.kernelFactory=create;
    spec.presets={
            {"轻微",{{ChorusRate,.8f},{ChorusDepth,.35f},{ChorusMix,.3f},{ChorusDelay,12}}},
            {"宽阔",{{ChorusRate,2.5f},{ChorusDepth,.6f},{ChorusMix,.45f},{ChorusDelay,20},{ChorusFeedback,.2f}}},
            {"反馈空间",{{ChorusRate,.9f},{ChorusDepth,.5f},{ChorusMix,.5f},{ChorusDelay,18},{ChorusFeedback,.55f}}} };
    spec.help="Rate LFO 速度，Depth 调制深度，Mix 干湿比，Delay 基础延迟（毫秒）。";
    return {std::move(spec),parameterSpecs(),{{"音频","Audio"},{"处理","Processing"},{"合唱","Chorus"},{"干湿比","Mix"},{"速率","Rate"},{"深度","Depth"},{"延迟","Delay"},{"反馈","Feedback"},{"轻微","Subtle"},{"宽阔","Wide"},{"反馈空间","Feedback Space"},{"Rate LFO 速度，Depth 调制深度，Mix 干湿比，Delay 基础延迟（毫秒）。","Rate sets LFO speed, Depth modulation depth, Mix dry/wet balance, and Delay base delay in milliseconds."}}};
}
} // namespace tri::nodes::chorus
