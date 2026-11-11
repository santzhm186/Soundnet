#pragma once
#include "Parameters.hpp"
#include "Translations.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.channel-filter
// soundnet-order: 33
// Independent SDK module; stable keys remain scoped to this node.
namespace tri::nodes::channel_filter {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.channel-filter",typeID,"通道过滤","音符",1,1,true,true,
             CapNoteSource|CapData|CapBypassable,TimeMode::None,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {ChannelFilter}, nullptr};
    spec.kernelFactory=create;
    spec.help="只放行指定 MIDI 通道（1–16）；0 表示全部通道，旁通时不过滤。";
    NodeModule result{std::move(spec),parameterSpecs()};
    appendTranslations(result);
    return result;
}
} // namespace tri::nodes::channel_filter
