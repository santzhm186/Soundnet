#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.count
// soundnet-order: 37
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::count {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.count",CountNode,"次数条件","逻辑",1,1,true,true,
             CapTriggerSource|CapData,TimeMode::None,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger)},
             {pout("trigger","触发",Signal::Trigger,RoleTrigger)}, {TriggerCount}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::count
