#pragma once
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.midi-input
// soundnet-order: 0
namespace tri::nodes::midi_input {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.midi-input",typeID,"本地 MIDI","工程与 I/O",1,1,true,true,
             CapNoteSource|CapTriggerSource|CapGenerator,TimeMode::None,0,
             {}, {pout("notes","音符",Signal::Midi,RoleMain)}, {}, nullptr};
    spec.kernelFactory=create;
    return {std::move(spec),parameterSpecs()};
}
} // namespace tri::nodes::midi_input
