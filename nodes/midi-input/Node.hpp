#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.midi-input
// soundnet-order: 0
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::midi_input {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.midi-input",MidiNode,"本地 MIDI","输入",1,1,true,true,
             CapNoteSource|CapTriggerSource|CapGenerator,TimeMode::None,0,
             {}, {pout("notes","音符",Signal::Midi,RoleMain)}, {}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::midi_input
