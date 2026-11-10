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
    assert(store.lastGraph.nodes.size()==7);
    auto sample=store.lastGraph.nodes[1].sample;
    assert(sample && sample->frames()>1000);

    graph[@"nodes"][1][@"parameters"][@"sampler.gain"]=@.4f;
    assert(store.replace(@{@"graph":graph},engine));
    assert(store.lastGraph.nodes[1].sample.get()==sample.get());

    graph[@"nodes"][1][@"asset"][@"name"]=@"pluck-renamed.wav";
    assert(store.replace(@{@"graph":graph},engine));
    assert(store.lastGraph.nodes[1].sample && store.lastGraph.nodes[1].sample.get()!=sample.get());

    engine.midi(0x90,60,100);
    float left[128]={0},right[128]={0},outL[128],outR[128],peak=0;
    for(int block=0;block<40;++block) {
        engine.render(left,right,outL,outR,128);
        for(int i=0;i<128;++i)peak=std::max(peak,std::fabs(outL[i]));
    }
    assert(peak>.01f);
    // An unrelated new node and a parameter edit must preserve the decoded
    // sample, including when the document has been canonicalized.
    NSString* why=nil;
    NSDictionary* canonical=triassets::canonicalGraph(graph,&why);
    assert(canonical);
    NSData* json=[NSJSONSerialization dataWithJSONObject:canonical options:0 error:nil];
    NSMutableDictionary* current=[NSJSONSerialization JSONObjectWithData:json
        options:NSJSONReadingMutableContainers error:nil];
    assert(tri::PreparedAssets::sameNodeAssets(current,current));
    assert(store.replace(@{@"graph":current},engine));
    auto v4Sample=store.lastGraph.nodes[1].sample;
    current[@"nodes"][1][@"parameters"][@"sampler.gain"]=@.25;
    [current[@"nodes"] addObject:[@{@"id":@90,@"nodeKey":@"soundnet.comment",@"x":@0,@"y":@0,
                                  @"parameters":@{}} mutableCopy]];
    assert(store.replace(@{@"graph":current},engine));
    assert(store.lastGraph.nodes[1].sample.get()==v4Sample.get());
    NSDictionary* accepted=store.current;
    current[@"nodes"][1][@"asset"][@"pcm"]=@"not-base64";
    assert(!store.replace(@{@"graph":current},engine));
    assert(store.current==accepted);
    assert(store.lastGraph.nodes[1].sample.get()==v4Sample.get());

    // The same permissive document policy reaches the actual host store. The
    // unavailable node and its raw fields survive, while known nodes can run.
    NSDictionary* unknown=@{@"version":@4,@"nodes":@[
        @{@"id":@1,@"nodeKey":@"example.missing",@"nodeVersion":@1,@"x":@0,@"y":@0,
          @"parameters":@{@"future":@7},@"texts":@{@"note":@"retain"}},
        @{@"id":@2,@"nodeKey":@"soundnet.noise",@"x":@200,@"y":@0,@"parameters":@{}},
        @{@"id":@3,@"nodeKey":@"soundnet.audio-output",@"nodeVersion":@2,@"x":@400,@"y":@0,@"parameters":@{}}],
        @"edges":@[@{@"source":@2,@"sourcePort":@"audio",@"target":@3,@"port":@"audio"}]};
    assert(store.replace(@{@"graph":unknown},engine));
    assert(store.lastGraph.nodes[0].unavailable);
    assert([store.current[@"graph"] isEqual:unknown]);
    engine.render(nullptr,nullptr,outL,outR,128);
    peak=0;for(float sample:outL)peak=std::max(peak,std::fabs(sample));
    assert(peak>0.01f);
    // A refresh between preparation and publication must not combine decoded
    // configuration from one catalog with factories from another catalog.
    tri::PreparedAssets prepared;
    assert(prepared.load(store.current));
    const auto preparedGeneration=prepared.catalog->generation;
    tri::publishNodeCatalog(tri::makeNodeCatalog(tri::builtinNodeModules()));
    assert(tri::nodeCatalogSnapshot()->generation!=preparedGeneration);
    assert(store.apply(prepared,engine));
    assert(store.catalogGeneration==preparedGeneration);
    assert(store.replace(store.current,engine));
    assert(store.catalogGeneration==tri::nodeCatalogSnapshot()->generation);
    puts("PASS: asset reuse, invalid update rollback, unavailable nodes and catalog-consistent publication.");
}}
