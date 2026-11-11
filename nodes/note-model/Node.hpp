#pragma once
#include "Parameters.hpp"
#include "Translations.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.note-model
// soundnet-order: 14
// Independent SDK module; stable keys remain scoped to this node.
namespace tri::nodes::note_model {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.note-model",typeID,"音符模型","音符",1,1,true,true,
             CapNoteSource|CapData|CapAssetModel,TimeMode::None,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)}, {}, "model"};
    spec.kernelFactory=create;
    spec.importExtensions={"mlmodel","mlpackage","mlmodelc","json"};
    spec.assetDropPriority=1;
    NodeModule result{std::move(spec),parameterSpecs()};
    appendTranslations(result);
    return result;
}
} // namespace tri::nodes::note_model
