#pragma once
#include "Parameters.hpp"
#include "Translations.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.chord
// soundnet-order: 5
// Independent SDK module; stable keys remain scoped to this node.
namespace tri::nodes::chord {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.chord",typeID,"和弦输入","输入",1,1,true,true,
             CapNoteSource|CapData|CapGenerator,TimeMode::Timed,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {ChordRun,ChordRoot,ChordQuality,ClockBPM}, nullptr};
    spec.kernelFactory=create;
    NodeModule result{std::move(spec),parameterSpecs()};
    appendTranslations(result);
    return result;
}
} // namespace tri::nodes::chord
