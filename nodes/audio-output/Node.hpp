#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.audio-output
// soundnet-order: 41
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::audio_output {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.audio-output",AudioOutputNode,"音频输出","输出",2,1,true,true,
             CapOutputSink|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleOutput,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)}, {Master,OutputWidth}, nullptr};
    spec.legacyAdapter=true;
    return {std::move(spec),{},
        {{"音频输出","Audio Output"},
         {"独立版使用项目选择的设备，AU 返回宿主音轨。","Uses the selected project device in standalone mode and returns audio to the host track in the Audio Unit."}}};
}
} // namespace tri::nodes::audio_output
