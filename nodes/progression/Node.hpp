#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.progression
// soundnet-order: 6
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::progression {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.progression",ProgressNode,"和声走向","输入",2,1,true,true,
             CapNoteSource|CapData,TimeMode::Timed,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional|PortFeedback)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {ProgressRun,ProgressLength,ProgressBars,
              ProgressRoot1,ProgressQuality1,ProgressRoot2,ProgressQuality2,
              ProgressRoot3,ProgressQuality3,ProgressRoot4,ProgressQuality4,
              ProgressRoot5,ProgressQuality5,ProgressRoot6,ProgressQuality6,
              ProgressRoot7,ProgressQuality7,ProgressRoot8,ProgressQuality8,ClockBPM}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::progression
