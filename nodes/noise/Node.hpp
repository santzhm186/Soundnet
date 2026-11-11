#pragma once
#include "../../platform/NodeDescriptor.hpp"
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.noise
// soundnet-order: 20
namespace tri::nodes::noise {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.noise",typeID,"噪声","发声",1,1,true,true,
             CapStream|CapGenerator|CapBypassable,TimeMode::None,0,
             {}, {pout("audio","音频",Signal::Audio,RoleMain)},
             {NoiseLevel,NoiseType}, nullptr};
    spec.kernelFactory=create;
    spec.help="Type  0 白噪声  1 粉红噪声。";
    return {std::move(spec),parameterSpecs(),{{"音频","Audio"},{"处理","Processing"},{"白噪声","White Noise"},{"粉红噪声","Pink Noise"},{"噪声","Noise"},{"类型","Type"},{"电平","Level"},{"Type  0 白噪声  1 粉红噪声。","Type: 0 White Noise · 1 Pink Noise."}}};
}
} // namespace tri::nodes::noise
