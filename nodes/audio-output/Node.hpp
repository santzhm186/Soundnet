#pragma once
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.audio-output
// soundnet-order: 41
namespace tri::nodes::audio_output {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.audio-output",typeID,"音频输出","输出",2,1,true,true,
             CapOutputSink|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleOutput,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)}, {Master,OutputWidth}, nullptr};
    spec.kernelFactory=create;
    spec.help="独立版使用项目选择的设备，AU 返回宿主音轨。";
    return {std::move(spec),parameterSpecs(),
        {{"音频输出","Audio Output"},
         {"独立版使用项目选择的设备，AU 返回宿主音轨。","Uses the selected project device in standalone mode and returns audio to the host track in the Audio Unit."}}};
}
} // namespace tri::nodes::audio_output
