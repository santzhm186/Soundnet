#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.note-echo
// soundnet-order: 31
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::note_echo {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.note-echo",EchoNode,"音符回声","处理",1,1,true,true,
             CapNoteSource|CapData|CapBypassable|CapTemporal,TimeMode::Timed,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {EchoTime,EchoRepeats,EchoFeedback,EchoPitch}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::note_echo
