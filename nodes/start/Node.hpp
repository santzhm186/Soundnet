#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.start
// soundnet-order: 34
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::start {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.start",StartNode,"开始触发器","逻辑",1,1,true,true,
             CapTriggerSource|CapData,TimeMode::None,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger)},
             {pout("trigger","触发",Signal::Trigger,RoleTrigger)}, {TriggerPulse}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::start
