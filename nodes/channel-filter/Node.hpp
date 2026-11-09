#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.channel-filter
// soundnet-order: 33
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::channel_filter {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.channel-filter",ChannelFilterNode,"通道过滤","处理",1,1,true,true,
             CapNoteSource|CapData|CapBypassable,TimeMode::None,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {ChannelFilter}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::channel_filter
