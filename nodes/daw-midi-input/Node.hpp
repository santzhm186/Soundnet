#pragma once
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.daw-midi-input
// soundnet-order: 1
namespace tri::nodes::daw_midi_input {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.daw-midi-input",typeID,"DAW MIDI","输入",1,1,true,true,
             CapNoteSource|CapTriggerSource|CapGenerator,TimeMode::None,0,
             {}, {pout("notes","音符",Signal::Midi,RoleMain)}, {}, nullptr};
    spec.kernelFactory=create;
    return {std::move(spec),parameterSpecs()};
}
} // namespace tri::nodes::daw_midi_input
