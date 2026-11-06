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

    bool load(NSDictionary *input) {
        if (![input isKindOfClass:NSDictionary.class] ||
            !triassets::decode(input,clip,model,nullptr)) return false;
        if(input[@"graph"] && !triassets::decodeGraph(input[@"graph"],graph,nullptr))return false;
        dictionary = CFBridgingRelease(CFPropertyListCreateDeepCopy(nullptr,
            (__bridge CFDictionaryRef)input,kCFPropertyListImmutable));
        return dictionary != nil;
    }
};

struct WorkflowAssetStore {
    NSDictionary *current = @{};
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
        if (!next.load(input)) return false;
        apply(next,engine);
        return true;
    }
};
}
