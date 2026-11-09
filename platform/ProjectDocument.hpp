#pragma once
#import <Foundation/Foundation.h>
#include "../src/NodeSpec.hpp"

namespace tri {
// Project templates are assembled from environment roles, not a node-type list.
// Subgraph tests and imported drafts may omit them; newly created projects may not.
inline NSMutableDictionary* newProjectDocument() {
    NSMutableArray* nodes=[NSMutableArray new];
    uint64_t instance=1;
    for(const auto& spec:nodeSpecs()) {
        if(spec.environmentRole==EnvironmentRole::None)continue;
        NSMutableDictionary* parameters=[NSMutableDictionary new];
        for(int id:spec.params)parameters[@(paramKey(id))]=@(nodeParamSpec(spec.type,id)->info->def);
        NSMutableDictionary* texts=[NSMutableDictionary new];
        for(const auto& text:spec.texts)if(text.defaultValue&&*text.defaultValue)texts[@(text.id)]=@(text.defaultValue);
        [nodes addObject:[@{@"id":@(instance),@"nodeKey":@(spec.key),@"nodeVersion":@(spec.nodeVersion),
            @"stateVersion":@(spec.stateVersion),@"x":@40,@"y":@(40+(instance-1)*140),
            @"parameters":parameters,@"texts":texts} mutableCopy]];
        ++instance;
    }
    return [@{@"version":@4,@"projectId":NSUUID.UUID.UUIDString,@"nodes":nodes,
        @"edges":[NSMutableArray new],@"settings":[@{@"environmentRequired":@YES} mutableCopy]} mutableCopy];
}
inline bool isEnvironmentNode(NSDictionary* node) {
    const char* key=[node[@"nodeKey"] isKindOfClass:NSString.class]?[node[@"nodeKey"] UTF8String]:"";
    const auto* spec=nodeSpec(nodeTypeForKey(key));
    return spec&&spec->environmentRole!=EnvironmentRole::None;
}
inline bool validProjectEnvironmentDocument(NSDictionary* document,NSString** why=nullptr) {
    if(![document[@"settings"] isKindOfClass:NSDictionary.class] || ![document[@"settings"][@"environmentRequired"] boolValue])return true;
    int timecode=0,settings=0;
    for(NSDictionary* node in document[@"nodes"]) {
        const auto* spec=nodeSpec(nodeTypeForKey([node[@"nodeKey"] isKindOfClass:NSString.class]?[node[@"nodeKey"] UTF8String]:""));
        if(!spec)continue;
        timecode+=spec->environmentRole==EnvironmentRole::Timecode;
        settings+=spec->environmentRole==EnvironmentRole::ProjectSettings;
    }
    if(timecode==1&&settings==1)return true;
    if(why)*why=@"工程必须包含一个时间码节点和一个项目设定节点。";
    return false;
}
}
