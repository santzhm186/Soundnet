// Project format v4: stable node keys, stable parameter keys, stable port IDs,
// and a one-way migration from the v3 documents that shipped before it.
#import <Foundation/Foundation.h>
#include "../src/WorkflowAssets.h"
#include "../src/GraphSpec.hpp"
#include <cassert>
#include <cstdio>

static int currentType(const char *key) {return tri::nodeTypeForKey(key);}
static int currentParameter(const char *node,const char *key) {return tri::nodeParamIdForKey(currentType(node),key);}

static NSDictionary *v4Document() {
    return @{
        @"version":@4, @"projectId":@"test-project",
        @"nodes":@[
            @{@"id":@1,@"nodeKey":@"soundnet.midi-input",@"x":@0,@"y":@0,@"parameters":@{}},
            @{@"id":@2,@"nodeKey":@"soundnet.voice-arranger",@"x":@200,@"y":@0,
              @"parameters":@{@"high.minimum":@72,@"high.maximum":@95,@"high.texture":@0,
                              @"middle.minimum":@55,@"middle.maximum":@71,@"middle.texture":@1,
                              @"low.minimum":@36,@"low.maximum":@54,@"low.texture":@2}},
            @{@"id":@3,@"nodeKey":@"soundnet.oscillator",@"x":@420,@"y":@0,
              @"parameters":@{@"osc1.wave":@2,@"osc1.level":@0.6}},
            @{@"id":@4,@"nodeKey":@"soundnet.audio-output",@"nodeVersion":@2,@"x":@640,@"y":@0,
              @"parameters":@{@"output.master":@0.8}}
        ],
        @"edges":@[
            @{@"source":@1,@"sourcePort":@"notes",@"target":@2,@"port":@"notes"},
            @{@"source":@2,@"sourcePort":@"middle",@"target":@3,@"port":@"notes"},
            @{@"source":@3,@"sourcePort":@"audio",@"target":@4,@"port":@"audio"}
        ]
    };
}

// Exercise the retained numeric-form parser with current registry IDs. This
// does not promise that historical pre-SDK numeric IDs map to new modules.
static NSDictionary *v3Document() {
    NSMutableDictionary *document=[v4Document() mutableCopy];
    NSMutableArray *nodes=[NSMutableArray new];
    for(NSDictionary *node in document[@"nodes"]) {
        NSMutableDictionary *row=[node mutableCopy];
        const int type=currentType([node[@"nodeKey"] UTF8String]);
        row[@"type"]=@(type);[row removeObjectForKey:@"nodeKey"];
        NSMutableDictionary *parameters=[NSMutableDictionary new];
        for(NSString *key in node[@"parameters"])
            parameters[[@(tri::nodeParamIdForKey(type,key.UTF8String)) stringValue]]=node[@"parameters"][key];
        if([node[@"id"] isEqual:@2])
            parameters[[@(currentParameter("soundnet.audio-output","output.master")) stringValue]]=@0.8;
        row[@"parameters"]=parameters;[nodes addObject:row];
    }
    document[@"version"]=@3;document[@"nodes"]=nodes;
    document[@"edges"]=@[
        @{@"source":@1,@"target":@2,@"port":@0},
        @{@"source":@2,@"target":@3,@"port":@0,@"sourcePort":@1},
        @{@"source":@3,@"target":@4,@"port":@0}];
    return document;
}

int main() {
    // 1. A canonical v4 document decodes with named ports and named parameters.
    {
        tri::GraphSpec graph;
        NSString *why=nil;
        assert(triassets::decodeGraph(v4Document(),graph,&why));
        assert(graph.projectId=="test-project");
        assert(graph.nodes.size()==4);
        assert(graph.nodes[1].type==currentType("soundnet.voice-arranger"));
        assert(graph.nodes[1].key=="soundnet.voice-arranger");
        assert(graph.nodes[1].parameters.at(currentParameter("soundnet.voice-arranger","high.minimum"))==72);
        assert(graph.nodes[2].parameters.at(currentParameter("soundnet.oscillator","osc1.wave"))==2);
        assert(graph.edges.size()==3);
        assert(graph.edges[1].port==0&&graph.edges[1].sourcePort==1);
        assert(graph.edges[1].sourcePortId=="middle");
        assert(graph.edges[1].portId=="notes");
    }
    // 2. v3 migrates to the same graph and then to a canonical document.
    {
        tri::GraphSpec legacy;
        NSString *why=nil;
        assert(triassets::decodeGraph(v3Document(),legacy,&why));
        assert(legacy.nodes.size()==4);
        assert(legacy.nodes[1].parameters.count(currentParameter("soundnet.audio-output","output.master"))==0);   // out of scope, dropped
        NSDictionary *canonical=triassets::canonicalGraph(v3Document(),&why);
        assert(canonical&&[canonical[@"version"] isEqual:@4]);
        NSArray *nodes=canonical[@"nodes"];
        assert([[nodes[1] objectForKey:@"nodeKey"] isEqual:@"soundnet.voice-arranger"]);
        assert([nodes[1][@"parameters"] objectForKey:@"high.minimum"]);
        assert([[canonical[@"edges"][1] objectForKey:@"sourcePort"] isEqual:@"middle"]);
        tri::GraphSpec migrated;
        assert(triassets::decodeGraph(canonical,migrated,&why));
        assert(migrated.nodes[1].parameters.at(currentParameter("soundnet.voice-arranger","high.minimum"))==72);
        assert(migrated.edges[1].sourcePort==1);
        assert(migrated.edges[1].port==0);
    }
    // 3. An unknown node type survives a round trip with its edges and assets.
    {
        NSMutableDictionary *document=[v4Document() mutableCopy];
        NSMutableArray *nodes=[document[@"nodes"] mutableCopy];
        [nodes addObject:@{@"id":@9,@"nodeKey":@"soundnet.future-renderer",@"x":@800,@"y":@0,
                           @"parameters":@{@"whatever":@1}}];
        NSMutableArray *edges=[document[@"edges"] mutableCopy];
        [edges addObject:@{@"source":@3,@"sourcePort":@"audio",@"target":@9,@"port":@"in"}];
        document[@"nodes"]=nodes;document[@"edges"]=edges;
        tri::GraphSpec graph;
        NSString *why=nil;
        assert(triassets::decodeGraph(document,graph,&why,false,true));
        assert(graph.nodes.size()==5);
        assert(graph.nodes[4].unavailable);
        assert(graph.nodes[4].key=="soundnet.future-renderer");
        assert(graph.edges.size()==4);
        // Strict callers (export, host publication) refuse it.
        assert(!triassets::decodeGraph(document,graph,&why,false,false));
        NSDictionary *canonical=triassets::canonicalGraph(document,&why);
        assert([canonical[@"nodes"] count]==5);
        assert([[canonical[@"nodes"][4] objectForKey:@"nodeKey"] isEqual:@"soundnet.future-renderer"]);
        assert([[canonical[@"edges"][3] objectForKey:@"port"] isEqual:@"in"]);
    }
    // 4. v4 is strict: unknown names, wrong-node parameters and unsupported
    //    bypass must not be guessed into something plausible.
    {
        NSMutableDictionary *document=[v4Document() mutableCopy];
        NSMutableArray *edges=[document[@"edges"] mutableCopy];
        edges[0]=@{@"source":@1,@"sourcePort":@"notes",@"target":@2,@"port":@"nonsense"};
        document[@"edges"]=edges;
        tri::GraphSpec graph;NSString *why=nil;
        assert(!triassets::decodeGraph(document,graph,&why));
        assert(why.length>0);
    }
    {
        NSMutableDictionary *document=[v4Document() mutableCopy];
        NSMutableArray *nodes=[document[@"nodes"] mutableCopy];
        NSMutableDictionary *arranger=[nodes[1] mutableCopy];
        arranger[@"parameters"]=@{@"filter.cutoff":@100};    // belongs to the filter node
        nodes[1]=arranger;document[@"nodes"]=nodes;
        tri::GraphSpec graph;NSString *why=nil;
        assert(!triassets::decodeGraph(document,graph,&why));
    }
    {
        NSMutableDictionary *document=[v4Document() mutableCopy];
        NSMutableArray *nodes=[document[@"nodes"] mutableCopy];
        NSMutableDictionary *osc=[nodes[2] mutableCopy];
        osc[@"bypass"]=@YES;                                // oscillators have no bypass
        nodes[2]=osc;document[@"nodes"]=nodes;
        tri::GraphSpec graph;NSString *why=nil;
        assert(!triassets::decodeGraph(document,graph,&why));
    }
    {
        NSMutableDictionary *document=[v4Document() mutableCopy];
        NSMutableArray *nodes=[document[@"nodes"] mutableCopy];
        NSMutableDictionary *arranger=[nodes[1] mutableCopy];
        arranger[@"parameters"]=@{@"high.minimum":@90,@"high.maximum":@70};
        nodes[1]=arranger;document[@"nodes"]=nodes;
        tri::GraphSpec graph;NSString *why=nil;
        assert(!triassets::decodeGraph(document,graph,&why));   // low voice above high voice
    }
    // 5. A discrete parameter refuses fractions in both formats.
    {
        NSMutableDictionary *document=[v4Document() mutableCopy];
        NSMutableArray *nodes=[document[@"nodes"] mutableCopy];
        NSMutableDictionary *osc=[nodes[2] mutableCopy];
        osc[@"parameters"]=@{@"osc1.wave":@1.5};
        nodes[2]=osc;document[@"nodes"]=nodes;
        tri::GraphSpec graph;NSString *why=nil;
        assert(!triassets::decodeGraph(document,graph,&why));
    }
    // 6. A node written by a newer behaviour version is editable but not
    //    exportable: the editor keeps it, strict callers refuse to pretend.
    {
        NSMutableDictionary *document=[v4Document() mutableCopy];
        NSMutableArray *nodes=[document[@"nodes"] mutableCopy];
        NSMutableDictionary *osc=[nodes[2] mutableCopy];
        osc[@"nodeVersion"]=@99;
        nodes[2]=osc;document[@"nodes"]=nodes;
        tri::GraphSpec graph;NSString *why=nil;
        std::vector<tri::Diagnostic> diagnostics;
        assert(triassets::decodeGraph(document,graph,&why,false,true,&diagnostics));
        bool reported=false;
        for(const auto& diagnostic:diagnostics)
            if(diagnostic.message.find("行为版本")!=std::string::npos)reported=true;
        assert(reported);
        assert(!triassets::decodeGraph(document,graph,&why,false,false));
    }
    // 7. A null version is a file error, not an exception.
    {
        tri::GraphSpec graph;NSString *why=nil;
        assert(!triassets::decodeGraph(@{@"version":[NSNull null],@"nodes":@[],@"edges":@[]},
                                       graph,&why));
        assert(why.length>0);
        assert(triassets::canonicalGraph(@{@"version":[NSNull null]},&why)==nil);
    }
    // 8. v3 custom names and assets survive canonicalisation.
    {
        NSMutableDictionary *document=[v3Document() mutableCopy];
        NSMutableArray *nodes=[document[@"nodes"] mutableCopy];
        NSMutableDictionary *osc=[nodes[2] mutableCopy];
        osc[@"name"]=@"Lead";
        osc[@"note"]=@"keep me";
        nodes[2]=osc;document[@"nodes"]=nodes;
        NSString *why=nil;
        NSDictionary *canonical=triassets::canonicalGraph(document,&why);
        assert(canonical);
        assert([[canonical[@"nodes"][2] objectForKey:@"name"] isEqual:@"Lead"]);
        assert([[canonical[@"nodes"][2] objectForKey:@"note"] isEqual:@"keep me"]);
    }
    // 9. A legal float endpoint such as 0.001 must not be rejected because the
    //    document stores it as a double.
    {
        NSMutableDictionary *document=[v3Document() mutableCopy];
        NSMutableArray *nodes=[document[@"nodes"] mutableCopy];
        [nodes addObject:@{@"id":@8,@"type":@(currentType("soundnet.sampler")),@"x":@0,@"y":@0,
                           @"parameters":@{[@(currentParameter("soundnet.sampler","sampler.attack")) stringValue]:@0.001,[@(currentParameter("soundnet.sampler","sampler.release")) stringValue]:@5.0}}];
        document[@"nodes"]=nodes;
        tri::GraphSpec graph;NSString *why=nil;
        assert(triassets::decodeGraph(document,graph,&why));
        assert(graph.nodes[4].parameters.at(currentParameter("soundnet.sampler","sampler.attack"))==0.001f);
        NSDictionary *canonical=triassets::canonicalGraph(document,&why);
        assert(canonical);
        assert([canonical[@"nodes"][4][@"parameters"][@"sampler.attack"] doubleValue]==
               double(0.001f));
        assert(triassets::decodeGraph(canonical,graph,&why));
    }
    // 10. Two edges into one ordinary input: v3 keeps the last (the old loader
    //     behaved that way), v4 refuses instead of silently dropping one.
    {
        NSMutableDictionary *document=[v3Document() mutableCopy];
        NSMutableArray *edges=[document[@"edges"] mutableCopy];
        [edges addObject:@{@"source":@1,@"sourcePort":@"notes",@"target":@2,@"port":@"notes"}];
        document[@"edges"]=edges;
        tri::GraphSpec graph;NSString *why=nil;
        std::vector<tri::Diagnostic> diagnostics;
        assert(triassets::decodeGraph(document,graph,&why,false,true,&diagnostics));
        int intoArranger=0;
        for(const auto& edge:graph.edges) if(edge.target==2&&edge.port==0)++intoArranger;
        assert(intoArranger==1);
        bool explained=false;
        for(const auto& diagnostic:diagnostics)
            if(diagnostic.message.find("重复连接")!=std::string::npos)explained=true;
        assert(explained);
        NSMutableDictionary *v4=[v4Document() mutableCopy];
        NSMutableArray *v4edges=[v4[@"edges"] mutableCopy];
        [v4edges addObject:@{@"source":@1,@"sourcePort":@"notes",@"target":@2,@"port":@"notes"}];
        v4[@"edges"]=v4edges;
        assert(!triassets::decodeGraph(v4,graph,&why));
    }
    {   // Text parameters: declared keys round-trip, undeclared keys are refused.
        NSDictionary *document=@{
            @"version":@4,@"projectId":@"texts",
            @"nodes":@[
                @{@"id":@1,@"nodeKey":@"soundnet.yue2",@"x":@0,@"y":@0,
                  @"parameters":@{@"gen.steps":@12,@"gen.seed":@90.0},
                  @"texts":@{@"yue2.style":@"warm piano",@"yue2.lyrics":@"[Verse]\nhello"}},
                @{@"id":@2,@"nodeKey":@"soundnet.audio-output",@"nodeVersion":@2,@"x":@200,@"y":@0,@"parameters":@{}}
            ],
            @"edges":@[@{@"source":@1,@"sourcePort":@"audio",@"target":@2,@"port":@"audio"}]
        };
        tri::GraphSpec graph;NSString *why=nil;
        assert(triassets::decodeGraph(document,graph,&why));
        assert(graph.nodes[0].texts.size()==2);
        assert(graph.nodes[0].texts["yue2.style"]=="warm piano");
        assert(graph.nodes[0].texts["yue2.lyrics"]=="[Verse]\nhello");
        NSMutableDictionary *bad=[document mutableCopy];
        NSMutableArray *nodes=[document[@"nodes"] mutableCopy];
        NSMutableDictionary *first=[nodes[0] mutableCopy];
        first[@"texts"]=@{@"yue2.typo":@"x"};
        nodes[0]=first;bad[@"nodes"]=nodes;
        assert(!triassets::decodeGraph(bad,graph,&why));   // undeclared key is fatal in v4
    }
    // Version values are checked before narrowing. Unsupported versions become
    // whole-node placeholders in editor mode, including future fields/ports.
    for(NSString* field in @[@"nodeVersion",@"stateVersion"]) {
        for(NSNumber* invalid in @[@0,@-1,@1.5,@65537]) {
            NSMutableDictionary* document=[v4Document() mutableCopy];
            NSMutableArray* nodes=[document[@"nodes"] mutableCopy];
            NSMutableDictionary* node=[nodes[2] mutableCopy];node[field]=invalid;
            nodes[2]=node;document[@"nodes"]=nodes;
            tri::GraphSpec graph;NSString* why=nil;
            assert(!triassets::decodeGraph(document,graph,&why,false,true));
        }
        NSMutableDictionary* document=[v4Document() mutableCopy];
        NSMutableArray* nodes=[document[@"nodes"] mutableCopy];
        NSMutableDictionary* node=[nodes[2] mutableCopy];node[field]=@2;
        node[@"parameters"]=@{@"future.parameter":@3};
        node[@"texts"]=@{@"future.text":@"keep"};
        nodes[2]=node;document[@"nodes"]=nodes;
        tri::GraphSpec graph;NSString* why=nil;
        assert(triassets::decodeGraph(document,graph,&why,false,true));
        assert(graph.nodes[2].unavailable&&graph.nodes[2].type==0);
        assert(!triassets::decodeGraph(document,graph,&why,false,false));
        assert([triassets::canonicalGraph(document,&why) isEqual:document]);
    }
    {
        NSMutableDictionary* document=[v4Document() mutableCopy];document[@"version"]=@4.5;
        tri::GraphSpec graph;NSString* why=nil;
        assert(!triassets::decodeGraph(document,graph,&why));
    }
    // Disk manifests keep the project portable with its companion directory.
    {
        NSFileManager *files=NSFileManager.defaultManager;
        NSURL *directory=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:
            NSUUID.UUID.UUIDString] isDirectory:YES];
        NSURL *original=[directory URLByAppendingPathComponent:@"Original" isDirectory:YES];
        NSURL *url=[original URLByAppendingPathComponent:@"Test.json"];
        float samples[]={.25f,-.5f,1.25f,-1.f};
        NSString *pcm=[[NSData dataWithBytes:samples length:sizeof(samples)] base64EncodedStringWithOptions:0];
        tri::MidiClip clip{{{0,0x92,60,90},{1,0x82,60,3}},1};
        NSString *why=nil;
        NSDictionary *midi=triassets::encodeClipAsset(@"Take",clip,&why);
        assert(midi);
        NSString *score=@"X:1\nK:C\nC D|\n";
        NSDictionary *document=@{@"version":@4,@"projectId":@"file-test",@"edges":@[],@"nodes":@[
            @{@"id":@1,@"nodeKey":@"soundnet.yue2",@"x":@0,@"y":@0,
              @"asset":@{@"name":@"Voice",@"rate":@48000,@"channels":@2,@"pcm":pcm},
              @"texts":@{@"yue2.style":@"warm",@"yue2.score":score}},
            @{@"id":@2,@"nodeKey":@"soundnet.midi-clip",
              @"nodeVersion":@(tri::nodeSpecForKey("soundnet.midi-clip")->nodeVersion),
              @"x":@200,@"y":@0,@"asset":midi}]};
        assert(triassets::writeProject(document,url,&why));
        NSDictionary *manifest=[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfURL:url]
                                                                  options:0 error:nil];
        assert([manifest[@"version"] isEqual:@6]);
        NSArray *rows=manifest[@"nodes"];
        assert(!rows[0][@"asset"]&&!rows[1][@"asset"]&&!rows[0][@"texts"][@"yue2.score"]);
        assert([rows[0][@"texts"][@"yue2.style"] isEqual:@"warm"]);
        assert([rows[0][@"assetRef"][@"path"] hasSuffix:@".wav"]);
        assert([rows[1][@"assetRef"][@"path"] hasSuffix:@".mid"]);
        assert([rows[0][@"textRefs"][@"yue2.score"][@"path"] hasSuffix:@".abc"]);
        NSURL *moved=[directory URLByAppendingPathComponent:@"Moved" isDirectory:YES];
        assert([files moveItemAtURL:original toURL:moved error:nil]);
        url=[moved URLByAppendingPathComponent:@"Test.json"];
        NSDictionary *resolved=triassets::readProject(url,&why);
        assert([resolved isEqual:document]);
        tri::GraphSpec graph;
        assert(triassets::decodeGraph(resolved,graph,&why,true,false));
        assert(graph.nodes[0].sample->samples[2]==1.25f&&graph.nodes[1].clip.events.size()==2);

        NSURL *asset=[moved URLByAppendingPathComponent:rows[0][@"assetRef"][@"path"]];
        assert([files removeItemAtURL:asset error:nil]);
        assert(!triassets::readProject(url,&why)&&[why containsString:rows[0][@"assetRef"][@"path"]]);
        NSMutableDictionary *broken=[manifest mutableCopy];
        NSMutableArray *brokenRows=[rows mutableCopy];
        NSMutableDictionary *row=[brokenRows[0] mutableCopy];
        row[@"assetRef"]=@{@"kind":@"audio",@"path":@"../outside.wav",@"name":@"Voice"};
        brokenRows[0]=row;broken[@"nodes"]=brokenRows;
        NSData *invalid=[NSJSONSerialization dataWithJSONObject:broken options:0 error:nil];
        assert([invalid writeToURL:url atomically:YES]);
        assert(!triassets::readProject(url,&why)&&[why containsString:@"越过"]);
        assert([files removeItemAtURL:directory error:nil]);
    }
    // Multiple resource histories are self-contained in memory/AU state and
    // portable on disk, even when their node module is missing or upgraded.
    {
        NSFileManager *files=NSFileManager.defaultManager;
        NSURL *directory=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString] isDirectory:YES];
        NSURL *original=[directory URLByAppendingPathComponent:@"Original" isDirectory:YES];
        NSURL *url=[original URLByAppendingPathComponent:@"History.json"];
        float samples[]={.25f,-.25f};
        NSDictionary *asset=@{@"name":@"First",@"rate":@48000,@"channels":@2,
            @"pcm":[[NSData dataWithBytes:samples length:sizeof(samples)] base64EncodedStringWithOptions:0]};
        NSString *firstScore=@"X:1\nK:C\nC|\n",*secondScore=@"X:2\nK:D\nD|\n";
        NSDictionary *node=@{@"id":@1,@"nodeKey":@"soundnet.yue2",@"nodeVersion":@1,@"stateVersion":@1,
            @"x":@0,@"y":@0,@"asset":asset,@"texts":@{@"yue2.score":firstScore}};
        node=tri::projectresources::appendEntry(node,@"generation.takes",@"First",
            @{@"audio":tri::projectresources::assetResource(@"audio",asset),
              @"score":tri::projectresources::textResource(@"First score",@"abc",firstScore)},@{@"date":@"first"});
        node=tri::projectresources::appendEntry(node,@"generation.takes",@"Second",
            @{@"score":tri::projectresources::textResource(@"Second score",@"abc",secondScore)},@{@"date":@"second"});
        NSDictionary *document=@{@"version":@4,@"projectId":@"history",@"nodes":@[node],@"edges":@[]};
        NSString *why=nil;tri::GraphSpec graph;
        assert(triassets::decodeGraph(document,graph,&why)); // AU/in-memory path
        assert(triassets::writeProject(document,url,&why));
        NSMutableDictionary *manifest=[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfURL:url]
            options:NSJSONReadingMutableContainers error:nil];
        NSDictionary *diskNode=manifest[@"nodes"][0];
        assert(!diskNode[@"resources"]&&[diskNode[@"resourceRefs"] count]==3);
        for(NSDictionary *ref in [diskNode[@"resourceRefs"] allValues]) {
            assert(![ref[@"path"] isAbsolutePath]);
            assert([ref[@"kind"] length]>0);
        }
        NSURL *moved=[directory URLByAppendingPathComponent:@"Moved" isDirectory:YES];
        assert([files moveItemAtURL:original toURL:moved error:nil]);
        url=[moved URLByAppendingPathComponent:@"History.json"];
        NSDictionary *reopened=triassets::readProject(url,&why);
        assert([reopened isEqual:document]);
        NSArray *takes=tri::projectresources::collection(reopened[@"nodes"][0],@"generation.takes");
        assert(takes.count==2);
        assert([tri::projectresources::entryResource(reopened[@"nodes"][0],takes[0],@"audio")[@"data"] isEqual:asset]);
        assert([tri::projectresources::entryResource(reopened[@"nodes"][0],takes[1],@"score")[@"text"] isEqual:secondScore]);
        // Unknown module: preserve text type and every resource without its descriptor.
        manifest[@"nodes"][0][@"nodeKey"]=@"example.absent";
        assert([[NSJSONSerialization dataWithJSONObject:manifest options:0 error:nil] writeToURL:url atomically:YES]);
        reopened=triassets::readProject(url,&why);
        assert(reopened&&triassets::decodeGraph(reopened,graph,&why,false,true)&&graph.nodes[0].unavailable);
        assert([reopened[@"nodes"][0][@"texts"][@"yue2.score"] isEqual:firstScore]);
        NSURL *copied=[directory URLByAppendingPathComponent:@"Copy/Other.json"];
        assert(triassets::writeProject(reopened,copied,&why));
        assert([triassets::readProject(copied,&why) isEqual:reopened]);
        // v5 string references remain readable with a missing module.
        manifest[@"version"]=@5;
        manifest[@"nodes"][0][@"textRefs"][@"yue2.score"]=diskNode[@"textRefs"][@"yue2.score"][@"path"];
        assert([[NSJSONSerialization dataWithJSONObject:manifest options:0 error:nil] writeToURL:url atomically:YES]);
        assert(triassets::readProject(url,&why));
        // The resource table must not permit a dangling collection reference.
        NSMutableDictionary *bad=[node mutableCopy];bad[@"resources"]=@{};
        assert(!tri::projectresources::validate(bad,&why));
        // Save failure must leave the previous manifest usable.
        NSData *before=[NSData dataWithContentsOfURL:copied];
        NSMutableDictionary *badDocument=[document mutableCopy];badDocument[@"nodes"]=@[bad];
        assert(!triassets::writeProject(badDocument,copied,&why));
        assert([[NSData dataWithContentsOfURL:copied] isEqual:before]);
        NSMutableDictionary *corruptNode=[node mutableCopy],*corruptResources=[node[@"resources"] mutableCopy];
        corruptResources[@"broken-midi"]=tri::projectresources::assetResource(@"clip",
            @{@"name":@"Broken",@"smf":@"AAECAw=="});
        corruptNode[@"resources"]=corruptResources;badDocument[@"nodes"]=@[corruptNode];
        assert(!triassets::writeProject(badDocument,copied,&why));
        assert([[NSData dataWithContentsOfURL:copied] isEqual:before]);
        assert([files removeItemAtURL:directory error:nil]);
    }
    // Existing v5 take logs are collected once into typed resources. A
    // missing archived take reports failure; old unarchived metadata remains
    // explicit rather than pretending that an earlier performance was saved.
    {
        NSURL *directory=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString] isDirectory:YES];
        [NSFileManager.defaultManager createDirectoryAtURL:directory withIntermediateDirectories:YES attributes:nil error:nil];
        float samples[]={.1f,.2f};
        NSDictionary *asset=@{@"name":@"Legacy take",@"rate":@48000,@"channels":@2,
            @"pcm":[[NSData dataWithBytes:samples length:sizeof(samples)] base64EncodedStringWithOptions:0]};
        NSURL *audio=[directory URLByAppendingPathComponent:@"Old.wav"];
        NSString *why=nil;
        assert([tri::projectresources::audioFileData(asset,&why) writeToURL:audio atomically:YES]);
        NSArray *takes=@[@{@"name":@"Older",@"file":@"",@"seconds":@0},
                         @{@"name":@"Archived",@"file":audio.path,@"seconds":@1}];
        NSString *log=[[NSString alloc]initWithData:[NSJSONSerialization dataWithJSONObject:takes options:0 error:nil] encoding:NSUTF8StringEncoding];
        NSDictionary *document=@{@"version":@4,@"nodes":@[@{@"id":@1,@"nodeKey":@"soundnet.yue2",@"x":@0,@"y":@0,
            @"asset":asset,@"texts":@{@"gen.takes":log,@"yue2.score":@"X:1\nK:C\nC|\n"}}],@"edges":@[]};
        NSURL *url=[directory URLByAppendingPathComponent:@"Old.json"];
        assert([[NSJSONSerialization dataWithJSONObject:document options:0 error:nil] writeToURL:url atomically:YES]);
        NSDictionary *imported=triassets::readProject(url,&why);
        assert(imported&&!imported[@"nodes"][0][@"texts"][@"gen.takes"]);
        NSDictionary *node=imported[@"nodes"][0];
        NSArray *history=tri::projectresources::collection(node,@"generation.takes");
        assert(history.count==2&&[history[0][@"metadata"][@"unavailable"] boolValue]);
        assert([tri::projectresources::entryResource(node,history[1],@"audio")[@"data"][@"pcm"] isEqual:asset[@"pcm"]]);
        assert(tri::projectresources::entryResource(node,history[1],@"score"));
        NSURL *converted=[directory URLByAppendingPathComponent:@"Converted.json"];
        assert(triassets::writeProject(imported,converted,&why));
        assert([NSFileManager.defaultManager removeItemAtURL:audio error:nil]);
        assert(triassets::readProject(converted,&why));
        assert(!triassets::readProject(url,&why)&&[why containsString:@"Old.wav"]);
        assert([NSFileManager.defaultManager removeItemAtURL:directory error:nil]);
    }
    puts("PASS: document versions, portable resource collections, missing modules and validation");
}
