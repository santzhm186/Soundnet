#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.arp
// soundnet-order: 11
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::arp {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.arp",ArpNode,"ARP","处理",2,1,true,true,
             CapNoteSource|CapData,TimeMode::Timed,0,
             {pin("notes","音符",Signal::Midi,RoleMain),
              pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional|PortFeedback)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {ArpOn,Pattern,ArpRepeat,Gate,ClockBPM}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::arp
