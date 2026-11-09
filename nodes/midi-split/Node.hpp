#pragma once
#include "../../platform/NodeDescriptor.hpp"
// soundnet-module: soundnet.midi-split
// soundnet-order: 101
#include "Kernel.hpp"
namespace tri::nodes::midi_split {
inline constexpr ParamInfo splitInfo{"Split note",0,127,60,"MIDI"};
inline NodeModule module() {
    using namespace tri::detail;
    NodeSpec spec{"soundnet.midi-split",nodeTypeID("soundnet.midi-split"),"MIDI 音区分流","处理",1,1,true,true,
        CapStream|CapNoteSource|CapMultiOutput,TimeMode::None,0,
        {pin("notes","音符",Signal::Midi,RoleMain)},
        {pout("low","低音区",Signal::Midi,RoleMain),pout("high","高音区",Signal::Midi,RoleMain)},
        {splitID},nullptr};
    spec.kernelFactory=create;
    return {std::move(spec),{{splitID,ParamSpec{&splitInfo,"split",true,nullptr,0,ParamGroup::Harmony}}},
        {{"MIDI 音区分流","MIDI Note Split"},{"处理","Processing"},{"音符","Notes"},
         {"低音区","Low Range"},{"高音区","High Range"},{"分界音高","Split note"}}};
}
}
