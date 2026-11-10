#pragma once
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.midi-output
// soundnet-order: 43
namespace tri::nodes::midi_output {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.midi-output",typeID,"MIDI 输出","输出",1,1,true,true,
             CapNoteSource|CapData,TimeMode::None,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)}, {MidiOutChannel}, nullptr};
    spec.kernelFactory=create;
    spec.help="独立版发送至 Soundnet MIDI Out；AU 使用宿主的 MIDI 输出回调。通道 0 跟随输入，1–16 强制输出通道。";
    return {std::move(spec),parameterSpecs()};
}
} // namespace tri::nodes::midi_output
