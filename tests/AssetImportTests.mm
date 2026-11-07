#import "../src/WorkflowAssets.h"
#include "../src/StandardMidiFile.hpp"
#include <cassert>
#include <cstdio>

int main() { @autoreleasepool {
    auto path=[](NSString *p) { return [NSURL fileURLWithPath:p]; };
    NSString *why=nil;
    NSDictionary *clip=triassets::importAsset(@"clip",path(@"examples/FourBeat.mid"),&why);
    assert(clip && [clip[@"notes"] intValue]==5 && [clip[@"beats"] doubleValue]==4);
    NSDictionary *model=triassets::importAsset(@"model",path(@"examples/PitchMirror.mlmodel"),&why);
    if (!model) fprintf(stderr,"%s\n",why.UTF8String);
    assert(model);
    NSDictionary *assets=@{@"model":model,@"clip":clip};
    NSData *json=[NSJSONSerialization dataWithJSONObject:assets options:0 error:nil];
    tri::MidiClip decoded; tri::NoteModel pitches;
    assert(triassets::decode([NSJSONSerialization JSONObjectWithData:json options:0 error:nil],decoded,pitches,&why));
    for (int i=0;i<128;++i) assert(pitches.pitches[i]==127-i);
    assert(decoded.events.size()==10 && decoded.events[1].beat==.75);
    assert(!triassets::decode(@{@"clip":@{@"name":@"bad",@"smf":@"!"}},decoded,pitches,&why));
    assert(decoded.lengthBeats==4 && pitches.pitches[0]==127);

    // Format 1, channel 2. Second track uses running status, sustain pedal, and
    // velocity-zero note-off. First track determines the trailing rest.
    const uint8_t multitrack[]={
        'M','T','h','d',0,0,0,6,0,1,0,2,0,96,
        'M','T','r','k',0,0,0,5,0x81,0x40,0xff,0x2f,0,
        'M','T','r','k',0,0,0,26,0,0x91,65,70,0,65,80,0,0xb1,64,127,48,0x91,65,0,0,65,0,48,0xb1,64,0,0,0xff,0x2f,0
    };
    auto multi=std::vector<uint8_t>(std::begin(multitrack),std::end(multitrack));
    auto parsed=tri::smf::parse(multi.data(),multi.size());
    assert(parsed.events.size()==4 && parsed.lengthBeats==2 && parsed.events.back().beat==1);
    auto invalid=multi;
    invalid[12]=0xe7;
    bool rejected=false;
    try { tri::smf::parse(invalid.data(),invalid.size()); } catch (...) { rejected=true; }
    assert(rejected);
    for (size_t n=0;n<multi.size();++n) {
        bool failed=false;
        try { tri::smf::parse(multi.data(),n); } catch (...) { failed=true; }
        assert(failed);
    }
    tri::GraphSpec bypassGraph;NSString *graphWhy=nil;
    NSDictionary *filterDocument=@{@"version":@3,@"nodes":@[
        @{@"id":@1,@"type":@(tri::FilterNode),@"x":@0,@"y":@0,@"bypass":@YES,
          @"parameters":@{@"1100":@0,@"1101":@8000}}],@"edges":@[]};
    assert(triassets::decodeGraph(filterDocument,bypassGraph,&graphWhy));
    assert(bypassGraph.nodes.size()==1 && bypassGraph.nodes[0].bypass);
    NSMutableDictionary *badBypass=[filterDocument mutableCopy];
    badBypass[@"nodes"]=@[@{@"id":@1,@"type":@(tri::FilterNode),@"x":@0,@"y":@0,@"bypass":@"yes",
                            @"parameters":@{}}];
    assert(!triassets::decodeGraph(badBypass,bypassGraph,&graphWhy));
    tri::GraphSpec noiseGraph;
    assert(triassets::decodeGraph(@{@"version":@3,@"nodes":@[@{@"id":@1,@"type":@(tri::NoiseNode),@"x":@0,@"y":@0,
        @"parameters":@{@"1120":@.3}}],@"edges":@[]},noiseGraph,&graphWhy));
    NSDictionary *example=[NSJSONSerialization JSONObjectWithData:
        [NSData dataWithContentsOfFile:@"examples/filter-reverb-noise.json"] options:0 error:nil];
    tri::GraphSpec exampleGraph;
    assert(triassets::decodeGraph(example,exampleGraph,&graphWhy));
    assert(exampleGraph.nodes.size()==7 && exampleGraph.edges.size()==6);
    NSDictionary *samplerExample=[NSJSONSerialization JSONObjectWithData:
        [NSData dataWithContentsOfFile:@"examples/sampler-pluck.json"] options:0 error:nil];
    tri::GraphSpec samplerExampleGraph;
    assert(triassets::decodeGraph(samplerExample,samplerExampleGraph,&graphWhy));
    assert(samplerExampleGraph.nodes.size()==5 && samplerExampleGraph.edges.size()==4);
    assert(samplerExampleGraph.nodes[1].sample && samplerExampleGraph.nodes[1].sample->frames()>1000);
    NSDictionary *clipExample=[NSJSONSerialization JSONObjectWithData:
        [NSData dataWithContentsOfFile:@"examples/audio-clip-compressor.json"] options:0 error:nil];
    tri::GraphSpec clipExampleGraph;
    assert(triassets::decodeGraph(clipExample,clipExampleGraph,&graphWhy));
    assert(clipExampleGraph.nodes.size()==5 && clipExampleGraph.edges.size()==4);
    assert(clipExampleGraph.nodes[2].sample && clipExampleGraph.nodes[2].sample->frames()>1000);
    tri::GraphSpec samplerGraph;
    float pcm[4]={.5f,0,-.5f,0};
    NSData *pcmData=[NSData dataWithBytes:pcm length:sizeof(pcm)];
    NSDictionary *samplerDocument=@{@"version":@3,@"nodes":@[
        @{@"id":@1,@"type":@(tri::SamplerNode),@"x":@0,@"y":@0,@"parameters":@{@"1130":@60},
          @"asset":@{@"name":@"pluck.wav",@"rate":@48000,@"channels":@1,
                      @"pcm":[pcmData base64EncodedStringWithOptions:0]}}],@"edges":@[]};
    assert(triassets::decodeGraph(samplerDocument,samplerGraph,&graphWhy));
    assert(samplerGraph.nodes.size()==1 && samplerGraph.nodes[0].sample &&
           samplerGraph.nodes[0].sample->frames()==4 && samplerGraph.nodes[0].sample->channels==1);
    NSMutableDictionary *badChannels=[samplerDocument mutableCopy];
    NSMutableDictionary *badNode=[samplerDocument[@"nodes"][0] mutableCopy];
    NSMutableDictionary *badAsset=[badNode[@"asset"] mutableCopy];
    badAsset[@"channels"]=@3;badNode[@"asset"]=badAsset;badChannels[@"nodes"]=@[badNode];
    assert(!triassets::decodeGraph(badChannels,samplerGraph,&graphWhy));
    tri::GraphSpec clipGraph;
    NSDictionary *clipDocument=@{@"version":@3,@"nodes":@[
        @{@"id":@1,@"type":@(tri::AudioClipNode),@"x":@0,@"y":@0,@"parameters":@{@"1140":@.8},
          @"asset":@{@"name":@"loop.wav",@"rate":@48000,@"channels":@1,
                      @"pcm":[pcmData base64EncodedStringWithOptions:0]}},
        @{@"id":@2,@"type":@(tri::CompressorNode),@"x":@200,@"y":@0,
          @"parameters":@{@"1150":@-20,@"1151":@8}}],@"edges":@[]};
    assert(triassets::decodeGraph(clipDocument,clipGraph,&graphWhy));
    assert(clipGraph.nodes.size()==2 && clipGraph.nodes[0].sample &&
           clipGraph.nodes[0].sample->frames()==4);
    puts("audio clip and compressor JSON decode");
    puts("sampler audio JSON decodes and validates");
    {
        NSMutableData *wav=[NSMutableData data];
        auto u16=[&](uint16_t v){uint8_t b[2]={uint8_t(v),uint8_t(v>>8)};[wav appendBytes:b length:2];};
        auto u32=[&](uint32_t v){uint8_t b[4]={uint8_t(v),uint8_t(v>>8),uint8_t(v>>16),uint8_t(v>>24)};[wav appendBytes:b length:4];};
        [wav appendBytes:"RIFF" length:4];u32(44);
        [wav appendBytes:"WAVEfmt " length:8];u32(16);u16(1);u16(1);u32(48000);u32(96000);u16(2);u16(16);
        [wav appendBytes:"data" length:4];u32(8);u16(16384);u16(0);u16(uint16_t(int16_t(-16384)));u16(0);
        NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:@"soundnet-import-test.wav"];
        assert([wav writeToFile:path atomically:YES]);
        NSDictionary *imported=triassets::importAsset(@"audio",[NSURL fileURLWithPath:path],&why);
        assert(imported && [imported[@"rate"] doubleValue]==48000 && [imported[@"channels"] intValue]==1);
        tri::GraphSpec importedGraph;
        assert(triassets::decodeGraph(@{@"version":@3,@"nodes":@[@{@"id":@1,@"type":@(tri::SamplerNode),
            @"x":@0,@"y":@0,@"parameters":@{},@"asset":imported}],@"edges":@[]},importedGraph,&graphWhy));
        assert(importedGraph.nodes[0].sample && importedGraph.nodes[0].sample->frames()==4);
        [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
        puts("audio file import round-trips through a graph asset");
    }
    puts("PASS: SMF 0/1, pedal and rests, malformed files, JSON portability, example graph, sampler/audio-clip assets, compressor and real Core ML pitch predictions.");
} }
