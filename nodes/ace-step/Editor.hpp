#pragma once
// soundnet.ace-step — dedicated inspector panel (generation, take list, audition).
#include "../../platform/NodeEditor.h"
#include "Task.hpp"
#include "../../platform/musicgen/NodePanel.hpp"
namespace tri::nodes::ace_step {
inline NSView* editor(NodeEditorContext context) {
    tri::musicgen::PanelSpec spec;
    spec.request=[](NSDictionary* node,NSDictionary* environment,NSString* workDir,bool feedback) {
        (void)feedback;  // ACE-Step has no score loop
        return makeRequest(node,environment,workDir);
    };
    spec.job=[](tri::musicgen::GenRequest request,NSString* label,NSString* folder,NSString* takes) {
        return makeJob(std::move(request),label,folder,takes);
    };
    spec.apply=applyResult;
    spec.defaultLabel=@"ACE-Step";
    spec.promptKey=@"ace.prompt";
    spec.promptMissing=context.text(@"请先填写描述。");
    spec.scoreKey=@"";
    return tri::musicgen::makeNodePanel(context,spec);
}
}
