#pragma once
#include "Parameters.hpp"
#include "Translations.hpp"
// soundnet-module: soundnet.comment
// soundnet-order: 40
// Independent SDK module; stable keys remain scoped to this node.
namespace tri::nodes::comment {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.comment",typeID,"注释","注释",1,1,true,true,
             0,TimeMode::None,0, {}, {}, {}, nullptr};

    spec.help="注释节点不参与音频或 MIDI；用上方名称字段记录说明文字。";
    NodeModule result{std::move(spec),parameterSpecs()};
    appendTranslations(result);
    return result;
}
} // namespace tri::nodes::comment
