#pragma once
#include "../../platform/NodeDescriptor.hpp"
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.mixer
// soundnet-order: 15
namespace tri::nodes::mixer {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.mixer",typeID,"混音器","处理",1,1,true,true,
             CapStream,TimeMode::Stream,0,
             {pin("in1","输入 1",Signal::Audio,RoleData),pin("in2","输入 2",Signal::Audio,RoleData),
              pin("in3","输入 3",Signal::Audio,RoleData),pin("in4","输入 4",Signal::Audio,RoleData)},
             {pout("audio","音频",Signal::Audio,RoleMain)}, {}, nullptr};
    spec.kernelFactory=create;
    return {std::move(spec),parameterSpecs(),{{"音频","Audio"},{"处理","Processing"},{"混音器","Mixer"},{"输入 1","Input 1"},{"输入 2","Input 2"},{"输入 3","Input 3"},{"输入 4","Input 4"}}};
}
} // namespace tri::nodes::mixer
