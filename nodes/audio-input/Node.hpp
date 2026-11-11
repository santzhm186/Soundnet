#pragma once
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.audio-input
// soundnet-order: 3
namespace tri::nodes::audio_input {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.audio-input",typeID,"音频输入","工程与 I/O",2,1,true,true,
             CapStream|CapGenerator,TimeMode::Stream,0,
             {}, {pout("audio","音频",Signal::Audio,RoleMain)}, {InputGain}, nullptr};
    spec.kernelFactory=create;
    return {std::move(spec),parameterSpecs(),
        {{"音频输入","Audio Input"},{"输入增益","Input Gain"},
         {"在 AU 中接收宿主的侧链输入。","Receives the host sidechain input in the Audio Unit."}}};
}
} // namespace tri::nodes::audio_input
