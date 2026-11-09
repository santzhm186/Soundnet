#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.midi-clip
// soundnet-order: 2
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::midi_clip {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.midi-clip",ClipNode,"MIDI Clip","输入",2,1,true,true,
             CapNoteSource|CapData|CapAssetClip|CapStream,TimeMode::Timed,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional|PortFeedback)},
             {pout("notes","音符",Signal::Midi,RoleMain),pout("cycle","循环完成",Signal::Trigger,RoleTrigger)},
             {ClipRun,ClipLoop,ClockBPM}, "clip", {{"clip.score","乐谱（ABC）"}}};
    for(auto& text:spec.texts) {text.multiline=true;text.readOnly=true;}
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::midi_clip
