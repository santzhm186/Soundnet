#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.metronome
// soundnet-order: 39
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::metronome {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.metronome",MetronomeNode,"节拍器","逻辑",1,1,true,true,
             CapStream|CapGenerator|CapBypassable|CapTemporal,TimeMode::Timed,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {MetronomeBPM,MetronomeLevel,MetronomeAccent,MetronomeBeats}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::metronome
