#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.oscillator
// soundnet-order: 10
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::oscillator {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.oscillator",Osc1Node,"振荡器","处理",1,1,true,true,
             CapStream,TimeMode::Stream,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {Wave1,Level1,Tune1,Attack,Decay,Sustain,Release}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::oscillator
