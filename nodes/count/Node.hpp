#pragma once
#include "Parameters.hpp"
#include "Translations.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.count
// soundnet-order: 37
// Independent SDK module; stable keys remain scoped to this node.
namespace tri::nodes::count {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.count",typeID,"次数条件","走带与控制",1,1,true,true,
             CapTriggerSource|CapData,TimeMode::None,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger)},
             {pout("trigger","触发",Signal::Trigger,RoleTrigger)}, {TriggerCount}, nullptr};
    spec.kernelFactory=create;
    spec.help="每收到 X 个信号后输出一次，再开始下一轮计数。可连接 Clip 的循环完成端口。";
    NodeModule result{std::move(spec),parameterSpecs()};
    appendTranslations(result);
    return result;
}
} // namespace tri::nodes::count
