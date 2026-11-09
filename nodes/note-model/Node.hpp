#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.note-model
// soundnet-order: 14
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::note_model {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.note-model",ModelNode,"音符模型","处理",1,1,true,true,
             CapNoteSource|CapData|CapAssetModel,TimeMode::None,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)}, {}, "model"};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::note_model
