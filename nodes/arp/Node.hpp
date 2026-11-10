#pragma once
#include "Parameters.hpp"
#include "Translations.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.arp
// soundnet-order: 11
// Independent SDK module; stable keys remain scoped to this node.
namespace tri::nodes::arp {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.arp",typeID,"ARP","处理",2,1,true,true,
             CapNoteSource|CapData,TimeMode::Timed,0,
             {pin("notes","音符",Signal::Midi,RoleMain),
              pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional|PortFeedback)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {ArpOn,Pattern,ArpRepeat,Gate,ClockBPM}, nullptr};
    spec.kernelFactory=create;
    spec.help="Rhythm  0 直拍  1 Bossa Nova  2 Reggae  3 Disco  4 十六分";
    NodeModule result{std::move(spec),parameterSpecs()};
    appendTranslations(result);
    return result;
}
} // namespace tri::nodes::arp
