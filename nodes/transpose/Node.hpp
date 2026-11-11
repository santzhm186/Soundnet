#pragma once
#include "Parameters.hpp"
#include "Translations.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.transpose
// soundnet-order: 30
// Independent SDK module; stable keys remain scoped to this node.
namespace tri::nodes::transpose {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.transpose",typeID,"移调","音符",2,1,true,true,
             CapNoteSource|CapData|CapBypassable,TimeMode::None,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {TransposeSemitones}, nullptr};
    spec.kernelFactory=create;
    spec.presets={
            {"升八度",{{TransposeSemitones,12}}},
            {"降八度",{{TransposeSemitones,-12}}} };
    spec.help="把输入音符按半音移调，范围外自动夹到 0–127。旁通时保持原音高。";
    NodeModule result{std::move(spec),parameterSpecs(),
        {{"移调","Transpose"},{"音符","Notes"},{"移调半音","Semitones"}}};
    appendTranslations(result);
    return result;
}
} // namespace tri::nodes::transpose
