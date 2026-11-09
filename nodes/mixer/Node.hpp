#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.mixer
// soundnet-order: 15
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::mixer {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.mixer",MixNode,"混音器","处理",1,1,true,true,
             CapStream,TimeMode::Stream,0,
             {pin("in1","输入 1",Signal::Audio,RoleData),pin("in2","输入 2",Signal::Audio,RoleData),
              pin("in3","输入 3",Signal::Audio,RoleData),pin("in4","输入 4",Signal::Audio,RoleData)},
             {pout("audio","音频",Signal::Audio,RoleMain)}, {}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::mixer
