#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.comment
// soundnet-order: 40
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::comment {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.comment",CommentNode,"注释","注释",1,1,true,true,
             0,TimeMode::None,0, {}, {}, {}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::comment
