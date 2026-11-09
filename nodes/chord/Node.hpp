#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.chord
// soundnet-order: 5
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::chord {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.chord",ChordNode,"和弦输入","输入",1,1,true,true,
             CapNoteSource|CapData|CapGenerator,TimeMode::Timed,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {ChordRun,ChordRoot,ChordQuality,ClockBPM}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::chord
