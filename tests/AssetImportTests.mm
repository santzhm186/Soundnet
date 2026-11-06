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
    puts("PASS: SMF 0/1, pedal and rests, malformed files, JSON portability and real Core ML pitch predictions.");
} }
