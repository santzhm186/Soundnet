#pragma once
#include "Parameters.hpp"
#include "Translations.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.midi-clip
// soundnet-order: 2
// Independent SDK module; stable keys remain scoped to this node.
namespace tri::nodes::midi_clip {
inline NodeModule module() {
    using namespace tri::detail;
    NodeSpec spec{"soundnet.midi-clip",typeID,"MIDI Clip","输入",2,1,true,true,
             CapNoteSource|CapData|CapAssetClip|CapStream|CapMidiCapture,TimeMode::Timed,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional|PortFeedback),
              pin("notes","MIDI 输入",Signal::Midi,RoleMain,PortOptional)},
             {pout("notes","音符",Signal::Midi,RoleMain),pout("cycle","循环完成",Signal::Trigger,RoleTrigger)},
             {ClipRun,ClipLoop,ClockBPM}, "clip", {{"clip.score","乐谱（ABC）"}}};
    for(auto& text:spec.texts) {text.multiline=true;text.readOnly=true;text.assetExtension="abc";}
    spec.kernelFactory=create;
    spec.importExtensions={"mid","midi"};
    spec.assetDropPriority=1;
    spec.importParameters={{ClipRun,1},{ClipLoop,1}};
    NodeModule result{std::move(spec),parameterSpecs(),{
        {"编辑 MIDI…","Edit MIDI…"},{"编辑工具","Edit Tool"},{"智能工具","Smart Tool"},{"铅笔","Pencil"},{"关闭","Off"},
        {"起点（拍）","Start (beats)"},{"长度（拍）","Length (beats)"},{"片段长度","Clip Length"},
        {"松键力度","Off Velocity"},{"控制器轨道","Controller Lane"},{"弯音","Pitch Bend"},
        {"通道触后","Channel Pressure"},{"复音触后","Poly Pressure"},{"音色切换","Program Change"},
        {"编号","Number"},{"位置（拍）","Beat"},{"数值","Value"},{"删除所选","Delete Selected"},
        {"调制","Modulation"},{"音量","Volume"},{"声像","Pan"},{"表情","Expression"},
        {"试听音符","Preview Note"},{"试听全部音符","Preview All Notes"},
        {"延音踏板","Sustain"},{"持音踏板","Sostenuto"},{"柔音踏板","Soft Pedal"},
        {"双击添加 · Shift 多选 · 滚动音高 · Shift 滚动时间 · ⌘ 滚动缩放",
         "Double-click to add · Shift to extend selection · Scroll pitch · Shift-scroll time · ⌘-scroll zoom"},
        {"请先添加音符或控制器事件","Add a note or controller event first"},
        {"MIDI 输入","MIDI In"},{"录制 MIDI","Record MIDI"},{"完成录制","Finish Recording"},
        {"取消录制","Cancel Recording"},{"正在录制 MIDI…","Recording MIDI…"},
        {"无法录制 MIDI。","Could not record MIDI."},
        {"无法保存 MIDI 录制。","Could not save the MIDI recording."},
        {"请先完成其他 MIDI 录制。","Finish the other MIDI recording first."},
        {"无法录制。请连接 MIDI 输入，并完成其他录制。","Connect MIDI In and finish any other recording first."},
        {"节点或接线已变化，录制未保存。","The node or its connection changed. The recording was discarded."},
        {"录制超出容量，原片段已保留。","The recording exceeded capacity. The previous clip was kept."},
        {"没有收到可保存的 MIDI 事件。","No MIDI events were received."},
        {"连接 MIDI 输入后点「录制 MIDI」，然后播放上游。完成录制会替换当前片段。",
         "Connect MIDI In, click Record MIDI, and start the upstream source. Finishing replaces the current clip."}
    }};
    appendTranslations(result);
    return result;
}
} // namespace tri::nodes::midi_clip
