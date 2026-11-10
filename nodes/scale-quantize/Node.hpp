#pragma once
#include "Parameters.hpp"
#include "Translations.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.scale-quantize
// soundnet-order: 26
// Independent SDK module; stable keys remain scoped to this node.
namespace tri::nodes::scale_quantize {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.scale-quantize",typeID,"音阶量化","处理",1,1,true,true,
             CapNoteSource|CapData|CapBypassable,TimeMode::None,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {QuantizeRoot,QuantizeScale,QuantizeMode}, nullptr};
    spec.kernelFactory=create;
    spec.presets={
            {"C 大调",{{QuantizeRoot,0},{QuantizeScale,0},{QuantizeMode,0}}},
            {"A 小调",{{QuantizeRoot,9},{QuantizeScale,1},{QuantizeMode,0}}},
            {"C 五声",{{QuantizeRoot,0},{QuantizeScale,3},{QuantizeMode,0}}} };
    spec.help="Root 调性（0=C），Scale 0 大调 1 小调 2 多利亚 3 五声 4 半音，Mode 0 最近 1 向上 2 向下。";
    NodeModule result{std::move(spec),parameterSpecs()};
    appendTranslations(result);
    return result;
}
} // namespace tri::nodes::scale_quantize
