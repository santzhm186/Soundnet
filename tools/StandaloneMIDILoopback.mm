// Exercise both virtual endpoints using a second CoreMIDI client. Disconnect the
// audio output before starting the standalone host, so this test remains silent.
#import "../app/TRIHost.h"
#include "../src/Engine.hpp"
#import <CoreMIDI/CoreMIDI.h>
#import <mach/mach_time.h>
#include <atomic>
#include <set>
#include <cstdio>

static std::set<MIDIEndpointRef> endpoints(bool sources) {
    std::set<MIDIEndpointRef> found;
    ItemCount count=sources ? MIDIGetNumberOfSources() : MIDIGetNumberOfDestinations();
    for (ItemCount i=0;i<count;++i) found.insert(sources ? MIDIGetSource(i) : MIDIGetDestination(i));
    return found;
}
static MIDIEndpointRef added(bool source,const std::set<MIDIEndpointRef>& before,NSString *name) {
    for (auto ep:endpoints(source)) {
        if (before.count(ep)) continue;
        CFStringRef key=nullptr;
        if (MIDIObjectGetStringProperty(ep,kMIDIPropertyName,&key)) continue;
        if ([CFBridgingRelease(key) isEqual:name]) return ep;
    }
    return 0;
}
struct Receiver {
    std::atomic<unsigned> noteOn{0},noteOff{0},otherPitches{0};
    static void event(void *context,MIDITimeStamp,MIDIUniversalMessage msg) {
        if (msg.type!=kMIDIMessageTypeChannelVoice1) return;
        auto& receiver=*static_cast<Receiver *>(context);
        auto& v=msg.channelVoice1;
        if (v.status!=kMIDICVStatusNoteOn && v.status!=kMIDICVStatusNoteOff) return;
        if (v.note.number!=71 || v.channel!=2) { ++receiver.otherPitches; return; }
        if (v.status==kMIDICVStatusNoteOn && v.note.velocity) ++receiver.noteOn;
        else ++receiver.noteOff;
    }
};
static void pause(double seconds) {
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]];
}
int main() { @autoreleasepool {
    Receiver received;
    Receiver *target=&received;
    MIDIClientRef client=0; MIDIPortRef monitor=0,sender=0;
    if (MIDIClientCreateWithBlock(CFSTR("Soundnet test host"),&client,nil) ||
        MIDIOutputPortCreate(client,CFSTR("Test send"),&sender) ||
        MIDIInputPortCreateWithProtocol(client,CFSTR("Test monitor"),kMIDIProtocol_1_0,&monitor,
            ^(const MIDIEventList *list,void*){ MIDIEventListForEachEvent(list,Receiver::event,target); })) return 2;
    auto sources=endpoints(true),destinations=endpoints(false);
    TRIHost *host=[TRIHost new];
    [host setValue:0 forParam:tri::RouteOutputIn];
    [host setValue:tri::DawMidiNode forParam:tri::RouteMidiOutIn];
    [host start];
    pause(.15);
    auto out=added(true,sources,@"Soundnet MIDI Out"),in=added(false,destinations,@"Soundnet MIDI In");
    if (!out || !in || MIDIPortConnectSource(monitor,out,nullptr)) return 3;
    auto send=[&](UInt32 word) {
        MIDIEventList list{};
        auto *p=MIDIEventListInit(&list,kMIDIProtocol_1_0);
        MIDIEventListAdd(&list,sizeof(list),p,mach_absolute_time(),1,&word);
        return MIDISendEventList(sender,in,&list);
    };
    send(MIDI1UPNoteOn(0,2,71,100)); pause(.15);
    send(MIDI1UPNoteOff(0,2,71,0)); pause(.15);
    // A software key should not enter the independently wired virtual DAW input.
    [host sendStatus:0x90 data1:68 data2:100]; pause(.05);
    [host sendStatus:0x80 data1:68 data2:0]; pause(.05);
    const bool endpointsOK=received.noteOn.load()==1 && received.noteOff.load()==1 && !received.otherPitches.load();
    // MIDI Start/Continue, Song Position and 24-PPQN timing from the same
    // external client drive a one-shot clip directly into the virtual source.
    send(MIDI1UPSystemCommon(0,kMIDIStatusStop,0,0)); pause(.05);
    const unsigned char smf[]={ 'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
        'M','T','r','k',0,0,0,12,0,0x92,71,110,96,0x82,71,0,0,0xff,0x2f,0 };
    NSString *encoded=[[NSData dataWithBytes:smf length:sizeof(smf)] base64EncodedStringWithOptions:0];
    if (![host replaceWorkflowAssets:@{@"clip":@{@"name":@"test.mid",@"smf":encoded}}]) return 5;
    [host setValue:tri::ClipNode forParam:tri::RouteMidiOutIn];
    [host setValue:0 forParam:tri::ClipLoop];
    [host setValue:1 forParam:tri::ClipRun];
    pause(.08);
    bool stopped=received.noteOn.load()==1;
    send(MIDI1UPSystemCommon(0,kMIDIStatusStart,0,0));
    for (int i=0;i<30;++i) {
        send(MIDI1UPSystemCommon(0,kMIDIStatusTimingClock,0,0)); pause(1./48.);
    }
    bool played=received.noteOn.load()==2 && received.noteOff.load()==2;
    send(MIDI1UPSystemCommon(0,kMIDIStatusStop,0,0));
    send(MIDI1UPSystemCommon(0,kMIDIStatusSongPosPointer,0,0));
    send(MIDI1UPSystemCommon(0,kMIDIStatusContinue,0,0)); pause(.08);
    send(MIDI1UPSystemCommon(0,kMIDIStatusStop,0,0)); pause(.08);
    bool located=received.noteOn.load()==3 && received.noteOff.load()==3;
    [host stop]; MIDIClientDispose(client);
    printf("CoreMIDI virtual source/destination: note-on %u, note-off %u, unrelated notes %u\n",
        received.noteOn.load(),received.noteOff.load(),received.otherPitches.load());
    printf("DAW clock: wait on stop %d, play to clip end %d, song position and continue %d\n",stopped,played,located);
    return endpointsOK && stopped && played && located ? 0 : 4;
}}
