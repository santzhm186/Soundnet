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
    spec.job=[](tri::musicgen::GenRequest request,NSString* label,NSString* folder,NSString* takes) {
        return makeJob(std::move(request),label,folder,takes);
    };
    spec.apply=applyResult;
    spec.defaultLabel=@"YuE2";
    spec.promptKey=@"yue2.style";
    spec.promptMissing=context.text(@"请先填写风格描述。");
    spec.scoreKey=@"yue2.score";
    return tri::musicgen::makeNodePanel(context,spec);
}
}
