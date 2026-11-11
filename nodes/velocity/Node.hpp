#pragma once
#include "Parameters.hpp"
#include "Translations.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.velocity
// soundnet-order: 13
// Independent SDK module; stable keys remain scoped to this node.
namespace tri::nodes::velocity {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.velocity",typeID,"力度自动化","音符",2,1,true,true,
             CapNoteSource|CapData,TimeMode::Timed,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {VelocityMin,VelocityMax,VelocityRate,VelocityShape,ClockBPM}, nullptr};
    spec.kernelFactory=create;
    NodeModule result{std::move(spec),parameterSpecs()};
    appendTranslations(result);
    return result;
}
} // namespace tri::nodes::velocity
