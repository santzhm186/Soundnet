#pragma once
#include "Parameters.hpp"
#include "Translations.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.metronome
// soundnet-order: 39
// Independent SDK module; stable keys remain scoped to this node.
namespace tri::nodes::metronome {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.metronome",typeID,"节拍器","逻辑",1,1,true,true,
             CapStream|CapGenerator|CapBypassable|CapTemporal,TimeMode::Timed,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {MetronomeBPM,MetronomeLevel,MetronomeAccent,MetronomeBeats}, nullptr};
    spec.kernelFactory=create;
    spec.presets={
            {"慢速",{{MetronomeBPM,80},{MetronomeLevel,.4f},{MetronomeAccent,1},{MetronomeBeats,4}}},
            {"快速",{{MetronomeBPM,160},{MetronomeLevel,.3f},{MetronomeAccent,1},{MetronomeBeats,4}}} };
    spec.help="开始触发后按 BPM 产生节拍，停止触发后静音。Beats 控制每小节拍数，Accent 控制第一拍重音。";
    NodeModule result{std::move(spec),parameterSpecs()};
    appendTranslations(result);
    return result;
}
} // namespace tri::nodes::metronome
