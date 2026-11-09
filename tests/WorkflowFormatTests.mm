// Project format v4: stable node keys, stable parameter keys, stable port IDs,
// and a one-way migration from the v3 documents that shipped before it.
#import <Foundation/Foundation.h>
#include "../src/WorkflowAssets.h"
#include "../src/GraphSpec.hpp"
#include <cassert>
#include <cstdio>

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

static NSDictionary *v3Document() {
    return @{
        @"version":@3,
        @"nodes":@[
            @{@"id":@1,@"type":@(tri::MidiNode),@"x":@0,@"y":@0,@"parameters":@{}},
            @{@"id":@2,@"type":@(tri::ArrangerNode),@"x":@200,@"y":@0,
              @"parameters":@{@"1024":@72,@"1025":@95,@"1026":@0,@"1027":@55,@"1028":@71,
                              @"1029":@1,@"1030":@36,@"1031":@54,@"1032":@2,
                              @"24":@0.8}},   // Master never belonged to the arranger.
            @{@"id":@3,@"type":@(tri::Osc1Node),@"x":@420,@"y":@0,
              @"parameters":@{@"0":@2,@"1":@0.6}},
            @{@"id":@4,@"type":@(tri::AudioOutputNode),@"nodeVersion":@2,@"x":@640,@"y":@0,
              @"parameters":@{@"24":@0.8}}
        ],
        @"edges":@[
            @{@"source":@1,@"target":@2,@"port":@0},
            @{@"source":@2,@"target":@3,@"port":@0,@"sourcePort":@1},
            @{@"source":@3,@"target":@4,@"port":@0}
        ]
    };
}

int main() {
    // 1. A canonical v4 document decodes with named ports and named parameters.
    {
        tri::GraphSpec graph;
        NSString *why=nil;
        assert(triassets::decodeGraph(v4Document(),graph,&why));
        assert(graph.projectId=="test-project");
        assert(graph.nodes.size()==4);
        assert(graph.nodes[1].type==tri::ArrangerNode);
        assert(graph.nodes[1].key=="soundnet.voice-arranger");
        assert(graph.nodes[1].parameters.at(tri::ArrHighMin)==72);
        assert(graph.nodes[2].parameters.at(tri::Wave1)==2);
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
        assert(legacy.nodes[1].parameters.count(tri::Master)==0);   // out of scope, dropped
        NSDictionary *canonical=triassets::canonicalGraph(v3Document(),&why);
        assert(canonical&&[canonical[@"version"] isEqual:@4]);
        NSArray *nodes=canonical[@"nodes"];
        assert([[nodes[1] objectForKey:@"nodeKey"] isEqual:@"soundnet.voice-arranger"]);
        assert([nodes[1][@"parameters"] objectForKey:@"high.minimum"]);
        assert([[canonical[@"edges"][1] objectForKey:@"sourcePort"] isEqual:@"middle"]);
        tri::GraphSpec migrated;
        assert(triassets::decodeGraph(canonical,migrated,&why));
        assert(migrated.nodes[1].parameters.at(tri::ArrHighMin)==72);
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
        [nodes addObject:@{@"id":@8,@"type":@(tri::SamplerNode),@"x":@0,@"y":@0,
                           @"parameters":@{@"1133":@0.001,@"1134":@5.0}}];
        document[@"nodes"]=nodes;
        tri::GraphSpec graph;NSString *why=nil;
        assert(triassets::decodeGraph(document,graph,&why));
        assert(graph.nodes[4].parameters.at(tri::SamplerAttack)==0.001f);
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
                @{@"id":@1,@"nodeKey":@"soundnet.ai-music",@"x":@0,@"y":@0,
                  @"parameters":@{@"ai.model":@1,@"ai.duration":@90.0},
                  @"texts":@{@"ai.prompt":@"warm piano",@"ai.lyrics":@"[Verse]\nhello"}},
                @{@"id":@2,@"nodeKey":@"soundnet.audio-output",@"nodeVersion":@2,@"x":@200,@"y":@0,@"parameters":@{}}
            ],
            @"edges":@[@{@"source":@1,@"sourcePort":@"audio",@"target":@2,@"port":@"audio"}]
        };
        tri::GraphSpec graph;NSString *why=nil;
        assert(triassets::decodeGraph(document,graph,&why));
        assert(graph.nodes[0].texts.size()==2);
        assert(graph.nodes[0].texts["ai.prompt"]=="warm piano");
        assert(graph.nodes[0].texts["ai.lyrics"]=="[Verse]\nhello");
        NSMutableDictionary *bad=[document mutableCopy];
        NSMutableArray *nodes=[document[@"nodes"] mutableCopy];
        NSMutableDictionary *first=[nodes[0] mutableCopy];
        first[@"texts"]=@{@"ai.typo":@"x"};
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
    puts("PASS: document versions, inert future nodes, parameter scope and port names");
}
