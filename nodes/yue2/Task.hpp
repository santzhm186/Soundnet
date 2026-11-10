#pragma once
// Generation uses platform-managed resources and publishes through the editor transaction.
#import <Foundation/Foundation.h>
#include "Node.hpp"
#include "../../platform/musicgen/GenerationArchive.hpp"

namespace tri::nodes::yue2 {

inline double parameter(NSDictionary* node,NSString* key,double fallback) {
    id value=node[@"parameters"][key];
    return [value isKindOfClass:NSNumber.class]?[value doubleValue]:fallback;
}
inline NSString* text(NSDictionary* node,NSString* key) {
    id value=node[@"texts"][key];
    return [value isKindOfClass:NSString.class]?value:@"";
}

// feedback = re-render from the edited score instead of planning a new one.
inline tri::musicgen::GenRequest makeRequest(NSDictionary* node,NSDictionary* environment,
                                             NSString* workDir,bool feedback) {
    tri::musicgen::GenRequest request;
    request.family="yue2";
    request.prompt=text(node,@"yue2.style").UTF8String ?: "";
    request.lyrics=text(node,@"yue2.lyrics").UTF8String ?: "";
    request.extraArgs=text(node,@"gen.extraArgs").UTF8String ?: "";
    request.execPath=text(node,@"gen.execPath").UTF8String ?: "";
    request.modelPath=text(node,@"gen.modelPath").UTF8String ?: "";
    request.arLora=text(node,@"yue2.arLora").UTF8String ?: "";
    request.narLora=text(node,@"yue2.narLora").UTF8String ?: "";
    request.cot=int(parameter(node,@"gen.cot",2));
    request.outMode=int(parameter(node,@"gen.output",0));
    request.variant=int(parameter(node,@"gen.variant",0));
    request.steps=int(parameter(node,@"gen.steps",8));
    request.seed=unsigned(parameter(node,@"gen.seed",0));
    if(feedback)request.abcText=text(node,@"yue2.score").UTF8String ?: "";
    request.workDir=workDir.UTF8String ?: "";
    (void)environment;
    return request;
}

inline soundnet::TaskService::Job makeJob(tri::musicgen::GenRequest request,NSString* nodeLabel) {
    return tri::musicgen::generationJob(std::move(request),nodeLabel);
}

// Control-thread applier: returns the replacement node content.
inline NSDictionary* applyResult(const soundnet::TaskResult& result,NSDictionary* current) {
    if(result.status!=soundnet::TaskStatus::Succeeded)return nil;
    auto payload=std::dynamic_pointer_cast<const tri::musicgen::GeneratedTake>(result.payload);
    if(!payload)return nil;
    NSMutableDictionary* replacement=[tri::musicgen::appendTake(current,*payload) mutableCopy];
    if(payload->asset)replacement[@"asset"]=[payload->asset copy];
    else replacement[@"asset"]=NSNull.null;
    NSMutableDictionary* texts=[current[@"texts"] isKindOfClass:NSDictionary.class]
        ? [current[@"texts"] mutableCopy] : [NSMutableDictionary new];
    [texts removeObjectForKey:@"gen.takes"];
    if(payload->score)texts[@"yue2.score"]=[payload->score copy];
    else [texts removeObjectForKey:@"yue2.score"];
    replacement[@"texts"]=texts;
    return replacement;
}

} // namespace tri::nodes::yue2
