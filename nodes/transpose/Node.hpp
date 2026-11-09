#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.transpose
// soundnet-order: 30
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::transpose {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.transpose",TransposeNode,"移调","处理",2,1,true,true,
             CapNoteSource|CapData|CapBypassable,TimeMode::None,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {TransposeSemitones}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{},
        {{"移调","Transpose"},{"音符","Notes"},{"移调半音","Semitones"}}};
}
} // namespace tri::nodes::transpose
