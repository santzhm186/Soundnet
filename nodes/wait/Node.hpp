#pragma once
#include "Parameters.hpp"
#include "Translations.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.wait
// soundnet-order: 36
// Independent SDK module; stable keys remain scoped to this node.
namespace tri::nodes::wait {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.wait",typeID,"等待","逻辑",1,1,true,true,
             CapTriggerSource|CapData|CapTemporal,TimeMode::Timed,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional|PortFeedback)},
             {pout("trigger","触发",Signal::Trigger,RoleTrigger)}, {WaitSeconds}, nullptr};
    spec.kernelFactory=create;
    spec.help="收到开始或停止信号后，等待指定秒数再转发。可排队等待多个信号。";
    NodeModule result{std::move(spec),parameterSpecs()};
    appendTranslations(result);
    return result;
}
} // namespace tri::nodes::wait
