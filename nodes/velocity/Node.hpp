#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.velocity
// soundnet-order: 13
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::velocity {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.velocity",VelocityNode,"力度自动化","处理",2,1,true,true,
             CapNoteSource|CapData,TimeMode::Timed,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {VelocityMin,VelocityMax,VelocityRate,VelocityShape,ClockBPM}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::velocity
