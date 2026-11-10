#pragma once
#include "Parameters.hpp"
#include "Translations.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.random
// soundnet-order: 38
// Independent SDK module; stable keys remain scoped to this node.
namespace tri::nodes::random {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.random",typeID,"随机触发","逻辑",1,1,true,true,
             CapTriggerSource|CapData,TimeMode::None,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger)},
             {pout("trigger","触发",Signal::Trigger,RoleTrigger)}, {RandomChance}, nullptr};
    spec.kernelFactory=create;
    spec.presets={
            {"半数",{{RandomChance,.5f}}},
            {"稀疏",{{RandomChance,.2f}}} };
    spec.help="收到触发后按 Chance 概率决定是否继续转发，适合生成随机段落或概率门限。";
    NodeModule result{std::move(spec),parameterSpecs()};
    appendTranslations(result);
    return result;
}
} // namespace tri::nodes::random
