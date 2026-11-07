#pragma once
#import "../src/WorkflowAssets.h"
#include "../src/Engine.hpp"

namespace tri {
// Accessed on the editor / AU property thread. The render thread sees only the
// compiled clip and model published by Engine.
struct PreparedAssets {
    NSDictionary *dictionary = nil;
    MidiClip clip{};
    NoteModel model{};
    GraphSpec graph;

    static bool sameNodeAssets(NSDictionary *next,NSDictionary *previous) {
        if(![next isKindOfClass:NSDictionary.class] || ![previous isKindOfClass:NSDictionary.class])return false;
        NSArray *nextNodes=next[@"nodes"],*previousNodes=previous[@"nodes"];
        if(![nextNodes isKindOfClass:NSArray.class] || ![previousNodes isKindOfClass:NSArray.class] ||
           nextNodes.count!=previousNodes.count)return false;
        NSMutableDictionary *byID=[NSMutableDictionary new];
        for(NSDictionary *node in previousNodes)if([node isKindOfClass:NSDictionary.class] && node[@"id"])
            byID[node[@"id"]]=node;
        for(NSDictionary *node in nextNodes) {
            if(![node isKindOfClass:NSDictionary.class])return false;
            NSDictionary *old=byID[node[@"id"]];
            if(!old || ![node[@"type"] isEqual:old[@"type"]])return false;
            id a=node[@"asset"],b=old[@"asset"];
            if(!(a==b || [a isEqual:b]))return false;
        }
        return true;
    }
    static void reuseNodeAssets(const GraphSpec& previous,GraphSpec& next) {
        for(auto& node:next.nodes) {
            for(const auto& old:previous.nodes)if(old.id==node.id) {
                node.clip=old.clip;node.model=old.model;node.sample=old.sample;break;
            }
        }
    }
    bool load(NSDictionary *input,NSDictionary *previousGraph=nil,const GraphSpec *previous=nil) {
        if (![input isKindOfClass:NSDictionary.class] ||
            !triassets::decode(input,clip,model,nullptr)) return false;
        if(input[@"graph"]) {
            bool reuse=previousGraph && previous && sameNodeAssets(input[@"graph"],previousGraph);
            if(!triassets::decodeGraph(input[@"graph"],graph,nullptr,!reuse))return false;
            if(reuse)reuseNodeAssets(*previous,graph);
        }
        dictionary = CFBridgingRelease(CFPropertyListCreateDeepCopy(nullptr,
            (__bridge CFDictionaryRef)input,kCFPropertyListImmutable));
        return dictionary != nil;
    }
};

struct WorkflowAssetStore {
    NSDictionary *current = @{};
    GraphSpec lastGraph;
    void apply(const PreparedAssets& next, Engine& engine) {
        auto changed = [&](NSString *key) {
            id previous = current[key], replacement = next.dictionary[key];
            return previous != replacement && ![previous isEqual:replacement];
        };
        if(changed(@"graph")) {
            if(next.dictionary[@"graph"])engine.setGraph(next.graph);
            else engine.clearGraph();
        }
        if (changed(@"clip")) engine.setClip(next.clip);
        if (changed(@"model")) engine.setModel(next.model);
        current = next.dictionary;
    }
    bool replace(NSDictionary *input, Engine& engine) {
        PreparedAssets next;
        if (!next.load(input,current[@"graph"],&lastGraph)) return false;
        apply(next,engine);
        lastGraph=next.graph;
        return true;
    }
};
}
