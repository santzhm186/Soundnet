#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.wait
// soundnet-order: 36
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::wait {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.wait",WaitNode,"等待","逻辑",1,1,true,true,
             CapTriggerSource|CapData|CapTemporal,TimeMode::Timed,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional|PortFeedback)},
             {pout("trigger","触发",Signal::Trigger,RoleTrigger)}, {WaitSeconds}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::wait
