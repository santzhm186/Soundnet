#pragma once
// ACE-Step generation task. The editor assembles an immutable GenRequest from
// the node configuration plus the project environment (archive folder) and
// submits it through NodeEditorContext::submitTask; the result applier writes
// the embedded asset and the take log back through commit. Objective-C++:
// editor/host translation units only.
#import <Foundation/Foundation.h>
#include "Node.hpp"
#include "../../platform/musicgen/GenerationArchive.hpp"

namespace tri::nodes::ace_step {

inline double parameter(NSDictionary* node,NSString* key,double fallback) {
    id value=node[@"parameters"][key];
    return [value isKindOfClass:NSNumber.class]?[value doubleValue]:fallback;
}
inline NSString* text(NSDictionary* node,NSString* key) {
    id value=node[@"texts"][key];
    return [value isKindOfClass:NSString.class]?value:@"";
}

inline tri::musicgen::GenRequest makeRequest(NSDictionary* node,NSDictionary* environment,
                                             NSString* workDir) {
    tri::musicgen::GenRequest request;
    request.family="ace_step";
    request.prompt=text(node,@"ace.prompt").UTF8String ?: "";
    request.lyrics=text(node,@"ace.lyrics").UTF8String ?: "";
    request.extraArgs=text(node,@"gen.extraArgs").UTF8String ?: "";
    request.execPath=text(node,@"gen.execPath").UTF8String ?: "";
    request.modelPath=text(node,@"gen.modelPath").UTF8String ?: "";
    request.seconds=parameter(node,@"gen.seconds",30);
    request.steps=int(parameter(node,@"gen.steps",8));
    request.seed=unsigned(parameter(node,@"gen.seed",0));
    request.workDir=workDir.UTF8String ?: "";
    (void)environment;
    return request;
}

inline soundnet::TaskService::Job makeJob(tri::musicgen::GenRequest request,NSString* nodeLabel,
                                          NSString* projectFolder,NSString* previousTakes) {
    return tri::musicgen::generationJob(std::move(request),nodeLabel,projectFolder,previousTakes);
}

// Control-thread applier: returns the replacement node content.
inline NSDictionary* applyResult(const soundnet::TaskResult& result,NSDictionary* current) {
    if(result.status!=soundnet::TaskStatus::Succeeded)return nil;
    auto payload=std::dynamic_pointer_cast<const tri::musicgen::GeneratedTake>(result.payload);
    if(!payload)return nil;
    NSMutableDictionary* replacement=[current mutableCopy];
    if(payload->asset)replacement[@"asset"]=[payload->asset copy];
    NSMutableDictionary* texts=[current[@"texts"] isKindOfClass:NSDictionary.class]
        ? [current[@"texts"] mutableCopy] : [NSMutableDictionary new];
    if(payload->takesJSON)texts[@"gen.takes"]=[payload->takesJSON copy];
    replacement[@"texts"]=texts;
    return replacement;
}

} // namespace tri::nodes::ace_step
