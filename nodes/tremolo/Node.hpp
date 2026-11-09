#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.tremolo
// soundnet-order: 27
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::tremolo {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.tremolo",TremoloNode,"颤音","处理",1,1,true,true,
             CapStream|CapBypassable|CapTemporal,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {TremoloRate,TremoloDepth,TremoloMix,TremoloStereo,TremoloSync,TremoloDivision,ClockBPM}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::tremolo
