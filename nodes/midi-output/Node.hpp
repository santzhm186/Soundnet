#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.midi-output
// soundnet-order: 43
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::midi_output {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.midi-output",MidiOutNode,"MIDI 输出","输出",1,1,true,true,
             CapNoteSource|CapData,TimeMode::None,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)}, {MidiOutChannel}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::midi_output
