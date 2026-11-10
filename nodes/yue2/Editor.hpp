#pragma once
// soundnet.yue2 — dedicated inspector panel (generation, take list, audition).
#include "../../platform/NodeEditor.h"
#include "Task.hpp"
#include "../../platform/musicgen/NodePanel.hpp"
namespace tri::nodes::yue2 {
inline NSView* editor(NodeEditorContext context) {
    tri::musicgen::PanelSpec spec;
    spec.request=[](NSDictionary* node,NSDictionary* environment,NSString* workDir,bool feedback) {
        return makeRequest(node,environment,workDir,feedback);
    };
    spec.job=[](tri::musicgen::GenRequest request,NSString* label) {
        return makeJob(std::move(request),label);
    };
    spec.apply=applyResult;
    spec.importScore=[](NodeEditorContext current,NSDictionary *node,NSString **why) {
        NSString *text=[node[@"texts"][@"yue2.score"] isKindOfClass:NSString.class]?node[@"texts"][@"yue2.score"]:@"";
        tri::abc::Score score;std::string error;
        tri::MidiClip clip;
        if(!tri::abc::parseClip(text.UTF8String ?: "",clip,score,error)) {
            if(why)*why=[NSString stringWithUTF8String:error.c_str()];return false;
        }
        if(score.bpm<40||score.bpm>240) {
            if(why)*why=current.text(@"MIDI 剪辑速度必须在 40–240 BPM 之间。");return false;
        }
        NSDictionary *asset=tri::projectresources::encodeClip(@"score.abc",clip,why);
        if(!asset||!current.createNode)return false;
        return bool(current.createNode(@"soundnet.midi-clip",
            @{@"asset":asset,@"texts":@{@"clip.score":text},@"parameters":@{@"clock.bpm":@(score.bpm)}}));
    };
    spec.defaultLabel=@"YuE2";
    spec.promptKey=@"yue2.style";
    spec.promptMissing=context.text(@"请先填写风格描述。");
    spec.scoreKey=@"yue2.score";
    return tri::musicgen::makeNodePanel(context,spec);
}
}
