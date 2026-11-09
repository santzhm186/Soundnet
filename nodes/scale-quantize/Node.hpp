#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.scale-quantize
// soundnet-order: 26
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::scale_quantize {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.scale-quantize",QuantizeNode,"音阶量化","处理",1,1,true,true,
             CapNoteSource|CapData|CapBypassable,TimeMode::None,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {QuantizeRoot,QuantizeScale,QuantizeMode}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::scale_quantize
