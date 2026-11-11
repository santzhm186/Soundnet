#pragma once
#include "Parameters.hpp"
#include "Translations.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.note-echo
// soundnet-order: 31
// Independent SDK module; stable keys remain scoped to this node.
namespace tri::nodes::note_echo {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.note-echo",typeID,"音符回声","音符",1,1,true,true,
             CapNoteSource|CapData|CapBypassable|CapTemporal,TimeMode::Timed,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {EchoTime,EchoRepeats,EchoFeedback,EchoPitch}, nullptr};
    spec.kernelFactory=create;
    spec.presets={
            {"短回声",{{EchoTime,.18f},{EchoRepeats,2},{EchoFeedback,.4f},{EchoPitch,0}}},
            {"八度回声",{{EchoTime,.25f},{EchoRepeats,3},{EchoFeedback,.5f},{EchoPitch,12}}} };
    spec.help="为输入音符安排多次延迟重复，每次可按 Pitch 移调并按 Feedback 衰减力度。原音立即通过。";
    NodeModule result{std::move(spec),parameterSpecs()};
    appendTranslations(result);
    return result;
}
} // namespace tri::nodes::note_echo
