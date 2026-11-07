#import "../app/WorkflowAssetStore.hpp"
#import <Foundation/Foundation.h>
#include <cassert>
#include <cmath>
#include <cstdio>

int main() { @autoreleasepool {
    tri::Engine engine;
    engine.prepare(48000);
    NSData *raw=[NSData dataWithContentsOfFile:@"examples/sampler-pluck.json"];
    assert(raw);
    NSMutableDictionary *graph=[NSJSONSerialization JSONObjectWithData:raw
        options:NSJSONReadingMutableContainers error:nil];
    assert(graph);
    tri::WorkflowAssetStore store;
    assert(store.replace(@{@"graph":graph},engine));
    assert(store.lastGraph.nodes.size()==5);
    auto sample=store.lastGraph.nodes[1].sample;
    assert(sample && sample->frames()>1000);

    graph[@"nodes"][1][@"parameters"][@"1131"]=@.4f;
    assert(store.replace(@{@"graph":graph},engine));
    assert(store.lastGraph.nodes[1].sample.get()==sample.get());

    engine.midi(0x90,60,100);
    float left[128]={0},right[128]={0},outL[128],outR[128],peak=0;
    for(int block=0;block<40;++block) {
        engine.render(left,right,outL,outR,128);
        for(int i=0;i<128;++i)peak=std::max(peak,std::fabs(outL[i]));
    }
    assert(peak>.01f);
    puts("PASS: asset store reuses embedded audio for graph-only edits.");
}}
