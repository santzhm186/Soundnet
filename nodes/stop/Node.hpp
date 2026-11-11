#pragma once
#include "Parameters.hpp"
#include "Translations.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.stop
// soundnet-order: 35
// Independent SDK module; stable keys remain scoped to this node.
namespace tri::nodes::stop {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.stop",typeID,"停止触发器","走带与控制",1,1,true,true,
             CapTriggerSource|CapData,TimeMode::None,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger)},
             {pout("trigger","触发",Signal::Trigger,RoleTrigger)}, {TriggerPulse}, nullptr};
    spec.kernelFactory=create;
    spec.triggerParameter=TriggerPulse;
    spec.triggerStarts=false;
    NodeModule result{std::move(spec),parameterSpecs()};
    appendTranslations(result);
    return result;
}
} // namespace tri::nodes::stop
