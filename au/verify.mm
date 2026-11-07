#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioToolbox/AUCocoaUIView.h>
#include "WorkflowEditorAccess.hpp"
#include <memory>
#include <dlfcn.h>
#include <CoreMIDI/CoreMIDI.h>
#include <vector>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cmath>

static void check(bool ok, const char* label) {
    if (!ok) { fprintf(stderr,"Failed: %s\n",label); exit(1); }
}
static void requireOK(OSStatus err,const char* label) {
    if (err) { fprintf(stderr,"Failed: %s: %d\n",label,int(err)); exit(1); }
}
static OSStatus silentSource(void*,AudioUnitRenderActionFlags* flags,const AudioTimeStamp*,UInt32,UInt32 frames,AudioBufferList* data) {
    for(UInt32 c=0; c<data->mNumberBuffers; ++c)
        std::memset(data->mBuffers[c].mData,0,frames*sizeof(float));
    *flags |= kAudioUnitRenderAction_OutputIsSilence;
    return noErr;
}
static OSStatus source(void*,AudioUnitRenderActionFlags*,const AudioTimeStamp*,UInt32,UInt32 frames,AudioBufferList* data) {
    for (UInt32 c=0;c<data->mNumberBuffers;++c)
        for (UInt32 f=0;f<frames;++f)
            static_cast<float*>(data->mBuffers[c].mData)[f] = c ? -.25f : .25f;
    return noErr;
}
// A host transport that can be stopped or seeked independently of sample time.
struct CapturedMIDI {
    unsigned ons=0,offs=0;
    MIDITimeStamp start=0,end=0;
};
static OSStatus outputMIDI(void *context,const AudioTimeStamp *time,UInt32 port,const MIDIPacketList *list) {
    auto &capture=*static_cast<CapturedMIDI *>(context);
    if (port!=0 || !time || !list || list->numPackets!=1) return kAudio_ParamError;
    const auto& packet=list->packet[0];
    if (packet.length==3 && packet.data[1]==71) {
        if ((packet.data[0]&0xf0)==0x90 && packet.data[2]) { ++capture.ons; capture.start=packet.timeStamp; }
        if ((packet.data[0]&0xf0)==0x80) { ++capture.offs; capture.end=packet.timeStamp; }
    }
    return noErr;
}
struct Playhead { double beat=0; bool playing=false; };
static OSStatus beatAndTempo(void *context,Float64 *beat,Float64 *bpm) {
    if (beat) *beat=static_cast<Playhead*>(context)->beat;
    if (bpm) *bpm=120;
    return noErr;
}
static OSStatus transport(void *context,Boolean *playing,Boolean*,Float64*,Boolean*,Float64*,Float64*) {
    if (playing) *playing=static_cast<Playhead*>(context)->playing;
    return noErr;
}

int main(int argc,char** argv) {
    @autoreleasepool {
        check(argc>=2,"supply path to Ternary.component");
        AudioComponentDescription desc={'aumu','Tern','Trny',0,0};
        AudioUnit unit=nullptr;
        if (!std::strcmp(argv[1],"--installed-oop")) {
            auto component=AudioComponentFindNext(nullptr,&desc);
            check(component,"find installed Ternary");
            UInt32 version=0;
            requireOK(AudioComponentGetVersion(component,&version),"installed version");
            check(version==0x23100,"system found installed Soundnet 2.49");
            __block AudioUnit remote=nullptr;
            __block OSStatus creation=kAudioUnitErr_Uninitialized;
            auto ready=dispatch_semaphore_create(0);
            AudioComponentInstantiate(component,kAudioComponentInstantiation_LoadOutOfProcess,
                ^(AudioComponentInstance instance, OSStatus result) {
                    remote=instance; creation=result; dispatch_semaphore_signal(ready);
                });
            check(!dispatch_semaphore_wait(ready,dispatch_time(DISPATCH_TIME_NOW,15*NSEC_PER_SEC)),"remote instance ready");
            requireOK(creation,"remote AudioComponentInstantiate");
            unit=remote;
        } else {
            NSString* bundlePath=[NSString stringWithUTF8String:argv[1]];
            NSString* binary=[bundlePath stringByAppendingPathComponent:@"Contents/MacOS/Soundnet"];
            void* lib=dlopen(binary.fileSystemRepresentation,RTLD_NOW);
            if (!lib) { fprintf(stderr,"%s\n",dlerror()); return 1; }
            auto factory=reinterpret_cast<AudioComponentFactoryFunction>(dlsym(lib,"TernaryUnitFactory"));
            check(factory,"exported factory");
            auto comp=AudioComponentRegister(&desc,CFSTR("Ternary: Ternary"),0x23100,factory);
            check(comp,"component registration");
            requireOK(AudioComponentInstanceNew(comp,&unit),"AudioComponentInstanceNew");
        }
        auto global=kAudioUnitScope_Global;
        UInt32 bytes=0;
        requireOK(AudioUnitGetPropertyInfo(unit,kAudioUnitProperty_ParameterList,global,0,&bytes,nullptr),"parameter list");
        check(bytes==sizeof(UInt32)*(tri::ParamCount-tri::defaultRouting.size()),"complete parameter list");
        for (int id=0;id<tri::RouteArpIn;++id) {
            AudioUnitParameterInfo info{};
            bytes=sizeof(info);
            requireOK(AudioUnitGetProperty(unit,kAudioUnitProperty_ParameterInfo,global,id,&info,&bytes),"parameter metadata");
            check(info.maxValue>=info.minValue && info.name[0],"usable parameter metadata");
            if (info.flags&kAudioUnitParameterFlag_CFNameRelease) CFRelease(info.cfNameString);
        }
        auto set=[&](int id,float v){requireOK(AudioUnitSetParameter(unit,id,global,0,v,0),"set parameter");};
        auto get=[&](int id) {
            float value=0;
            requireOK(AudioUnitGetParameter(unit,id,global,0,&value),"read parameter");
            return value;
        };
        // Remote AU parameter writes and the host mirror are asynchronous. Let a
        // simulated host deliver its control queue between separate edit/save actions.
        auto finishHostTurn=[&] {
            if (!std::strcmp(argv[1],"--installed-oop"))
                [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.08]];
        };
        auto workflow=std::make_unique<ternaryAU::EditorAccess>(unit);
        auto routes=[&] {
            requireOK(workflow->refresh(true),"read workflow through editor");
            return workflow->state.graph;
        };
        auto setRoutes=[&](const tri::Routing& graph) {return workflow->setGraph(graph);};
        auto coordinate=[&](int id) {
            requireOK(workflow->refresh(true),"read canvas position through editor");
            return workflow->state.layout[id-tri::layout::first];
        };
        auto putCoordinate=[&](int id,float v) {
            requireOK(workflow->setPosition(id,v),"write canvas position through editor");
        };
        auto newGraph=tri::defaultRouting;
        newGraph[tri::RouteOsc1In-tri::RouteArpIn]=tri::MidiNode;
        newGraph[tri::RouteOutputIn-tri::RouteArpIn]=tri::MixNode;
        // Include both a new slot and a previously unsupported source in the AU
        // editor command path (the out-of-process bridge uses ClassInfo).
        newGraph[tri::routeIndex(tri::RouteModelIn)]=tri::DawMidiNode;
        requireOK(setRoutes(newGraph),"rewire ARP bypass and audio output");
        const unsigned char smf[]={ 'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
            'M','T','r','k',0,0,0,12,0,0x90,69,110,96,0x80,69,0,0,0xff,0x2f,0 };
        NSString *encoded=[[NSData dataWithBytes:smf length:sizeof(smf)] base64EncodedStringWithOptions:0];
        NSDictionary *assets=@{@"clip":@{@"name":@"test.mid",@"smf":encoded}};
        requireOK(workflow->setAssets(assets),"load embedded clip through editor");
        check([workflow->readAssets() isEqual:assets],"clip available through editor");
        check(workflow->setAssets(@{@"clip":@{@"name":@"bad",@"smf":@"!?"}})!=noErr &&
              [workflow->readAssets() isEqual:assets],"bad clip cannot overwrite a working one");
        auto cycle=newGraph;
        cycle[tri::RouteEQIn-tri::RouteArpIn]=tri::DelayNode;
        check(setRoutes(cycle)!=noErr && routes()==newGraph,"cycle rejected without changing cables");
        putCoordinate(tri::layout::x(tri::EQNode),215.5f);
        putCoordinate(tri::layout::y(tri::EQNode),425);
        putCoordinate(tri::layout::visibility,511);
        set(tri::ArpRepeat,6);
        set(tri::Osc1Attack,.12f);
        set(tri::Osc2Sustain,.38f);
        set(tri::Osc3Release,2.75f);
        set(tri::Pattern,2);
        finishHostTurn();
        CFPropertyListRef preset=nullptr;
        bytes=sizeof(preset);
        requireOK(AudioUnitGetProperty(unit,kAudioUnitProperty_ClassInfo,global,0,&preset,&bytes),"save state");
        requireOK(setRoutes(tri::defaultRouting),"reset graph before recall");
        requireOK(workflow->setAssets(@{}),"clear assets before recall");
        putCoordinate(tri::layout::x(tri::EQNode),42);
        putCoordinate(tri::layout::visibility,1023);
        set(tri::ArpRepeat,1);
        set(tri::Osc1Attack,tri::Follow);
        set(tri::Osc2Sustain,tri::Follow);
        set(tri::Osc3Release,tri::Follow);
        set(tri::Pattern,1);
        finishHostTurn();
        requireOK(AudioUnitSetProperty(unit,kAudioUnitProperty_ClassInfo,global,0,&preset,sizeof(preset)),"restore state");
        finishHostTurn();
        check(routes()==newGraph && coordinate(tri::layout::x(tri::EQNode))==215.5f
              && coordinate(tri::layout::y(tri::EQNode))==425 && coordinate(tri::layout::visibility)==511,
              "1.2 cables, layout and visibility round trip");
        requireOK(workflow->refresh(true),"editor workflow snapshot");
        auto ui=workflow->state;
        check(ui.graph==newGraph && ui.layout[tri::layout::x(tri::EQNode)-tri::layout::first]==215.5f,
              "editor snapshot agrees with saved wiring and coordinates");
        check([workflow->readAssets() isEqual:assets],"AU preset recalls embedded MIDI clip");
        CFRelease(preset);
        check(get(tri::Pattern)==2 && get(tri::ArpRepeat)==6 && get(tri::Osc1Attack)==.12f
              && get(tri::Osc2Sustain)==.38f && get(tri::Osc3Release)==2.75f,"1.1 patch round trip");
        set(tri::DelayMix,0);
        finishHostTurn();
        Float64 tail=0;
        bytes=sizeof(tail);
        requireOK(AudioUnitGetProperty(unit,kAudioUnitProperty_TailTime,global,0,&tail,&bytes),"tail duration");
        check(tail==2.75,"tail includes longest independent oscillator release");

        // This fixture was exported using the released 1.0 binary. Test loading it
        // after an edited 1.1 patch, where missing fields must reset to neutral.
        NSString* here=[[NSString stringWithUTF8String:__FILE__] stringByDeletingLastPathComponent];
        NSString* v11path=[here stringByAppendingPathComponent:@"fixtures/Ternary-1.1-Reggae.plist"];
        NSDictionary* oneOne=[NSDictionary dictionaryWithContentsOfFile:v11path];
        check(oneOne!=nil,"1.1 fixture loaded");
        CFPropertyListRef state11=(__bridge CFPropertyListRef)oneOne;
        requireOK(AudioUnitSetProperty(unit,kAudioUnitProperty_ClassInfo,global,0,&state11,sizeof(state11)),"restore exported 1.1 state");
        finishHostTurn();
        check(workflow->readAssets().count==0,"old preset clears embedded assets");
        check(get(tri::ArpRepeat)==6 && get(tri::Osc3Release)==1.25f && routes()==tri::defaultRouting,
              "1.1 controls and original signal flow restored");
        check(coordinate(tri::layout::visibility)==1023 && coordinate(tri::layout::x(tri::EQNode))==-1,
              "old preset resets node visibility and positions");
        requireOK(setRoutes(newGraph),"edit cables before loading 1.0");
        NSString* path=[here stringByAppendingPathComponent:@"fixtures/Ternary-1.0-Reggae.plist"];
        NSDictionary* legacy=[NSDictionary dictionaryWithContentsOfFile:path];
        check(legacy!=nil,"1.0 fixture loaded");
        CFPropertyListRef old=(__bridge CFPropertyListRef)legacy;
        requireOK(AudioUnitSetProperty(unit,kAudioUnitProperty_ClassInfo,global,0,&old,sizeof(old)),"restore exported 1.0 state");
        finishHostTurn();
        check(routes()==tri::defaultRouting && get(tri::Pattern)==2 && get(tri::Attack)==.34f && get(tri::Release)==.81f,"legacy parameters restored");
        for (int i=tri::Osc1Attack; i<tri::RouteArpIn; ++i) {
            const float actual=get(i);
            if(actual!=tri::params[i].def) fprintf(stderr,"ID %d read %.5f expected %.5f\n",i,actual,tri::params[i].def);
            check(actual==tri::params[i].def,"legacy defaults");
        }
        AudioUnitParameterStringFromValue display={tri::Osc2Release,nullptr,nullptr};
        bytes=sizeof(display);
        requireOK(AudioUnitGetProperty(unit,kAudioUnitProperty_ParameterStringFromValue,global,0,&display,&bytes),"host Follow display");
        check(display.outString && CFEqual(display.outString,CFSTR("Follow")),"Follow sentinel rendered");
        CFRelease(display.outString);
        set(tri::Attack,.012f);
        constexpr UInt32 frames=512;
        std::array<float,frames> l{},r{};
        struct Stereo { UInt32 n; AudioBuffer buf[2]; };
        Stereo stereo={2,{{1,frames*4,l.data()},{1,frames*4,r.data()}}};
        AudioTimeStamp time{}; time.mFlags=kAudioTimeStampSampleTimeValid;
        AudioUnitRenderActionFlags flags=0;
        auto render=[&] {
            requireOK(AudioUnitRender(unit,&flags,&time,0,frames,reinterpret_cast<AudioBufferList*>(&stereo)),"AudioUnitRender");
            time.mSampleTime+=frames;
        };
        if (!std::strcmp(argv[1],"--installed-oop")) {
            // A remote AU proxy pulls all declared input buses before asking the AU
            // for its output. Model a DAW's unassigned sidechain with silence.
            AURenderCallbackStruct silent={silentSource,nullptr};
            requireOK(AudioUnitSetProperty(unit,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&silent,sizeof(silent)),"connect silent remote sidechain");
        }
        requireOK(AudioUnitInitialize(unit),"initialize");
        set(tri::ArpOn,0); set(tri::DelayMix,0);
        requireOK(MusicDeviceMIDIEvent(unit,0x90,60,100,128),"send MIDI on");
        render();
        check(std::all_of(l.begin(),l.begin()+128,[](float x){return fabsf(x)<1e-6f;}),"silence until MIDI offset");
        check(std::any_of(l.begin()+128,l.end(),[](float x){return fabsf(x)>0.001f;}),"synth sounded after MIDI onset");
        requireOK(MusicDeviceMIDIEvent(unit,0x80,60,0,0),"send MIDI off");
        requireOK(AudioUnitUninitialize(unit),"uninitialize");
        AURenderCallbackStruct cb={source,nullptr};
        requireOK(AudioUnitSetProperty(unit,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&cb,sizeof(cb)),"connect sidechain");
        requireOK(AudioUnitInitialize(unit),"reinitialize with sidechain");
        render();
        check(l[450]>0.01f && r[450]<-0.01f,"stereo sidechain reached output");
        auto muted=tri::defaultRouting;
        muted[tri::RouteOutputIn-tri::RouteArpIn]=tri::NoNode;
        requireOK(setRoutes(muted),"disconnect output on running AU");
        render();
        check(std::all_of(l.begin()+128,l.end(),[](float x){return std::abs(x)<1e-6f;}),"disconnected cable changes rendered audio");
        bool direct=std::strcmp(argv[1],"--installed-oop")!=0;
        auto peak=[&]{ float v=0; for(float x:l) v=std::max(v,std::abs(x)); return v; };
        tri::Routing daw{};
        daw[tri::routeIndex(tri::RouteOsc1In)]=tri::DawMidiNode;
        daw[tri::routeIndex(tri::RouteOutputIn)]=tri::Osc1Node;
        requireOK(setRoutes(daw),"connect explicit DAW MIDI to oscillator");
        requireOK(AudioUnitReset(unit,global,0),"clear held notes");
        requireOK(workflow->sendLocalMIDI({0x90,69,110}),"onscreen key input");
        render(); check(peak()<1e-5,"onscreen key isolated from DAW port");
        requireOK(workflow->sendLocalMIDI({0x80,69,0}),"release onscreen key");
        requireOK(MusicDeviceMIDIEvent(unit,0x90,69,110,0),"DAW track input");
        render(); check(peak()>.001,"DAW track reaches explicit DAW node");
        requireOK(MusicDeviceMIDIEvent(unit,0x80,69,0,0),"release DAW note");
        CapturedMIDI captured; // callback storage lives until the host disposes the AU
        if (direct) {
            CFArrayRef outputs=nullptr;
            bytes=sizeof(outputs);
            requireOK(AudioUnitGetProperty(unit,kAudioUnitProperty_MIDIOutputCallbackInfo,global,0,&outputs,&bytes),"advertise MIDI port to host");
            check(CFArrayGetCount(outputs)==1,"one MIDI output");
            CFRelease(outputs);
            AUMIDIOutputCallbackStruct delivery{outputMIDI,&captured};
            requireOK(AudioUnitSetProperty(unit,kAudioUnitProperty_MIDIOutputCallback,global,0,&delivery,sizeof(delivery)),"register host MIDI delivery");
            tri::Routing passthrough{};
            passthrough[tri::routeIndex(tri::RouteMidiOutIn)]=tri::DawMidiNode;
            requireOK(setRoutes(passthrough),"route DAW notes directly to MIDI out");
            requireOK(MusicDeviceMIDIEvent(unit,0x92,71,100,127),"pass through note-on");
            requireOK(MusicDeviceMIDIEvent(unit,0x82,71,0,322),"pass through note-off");
            render();
            check(captured.ons==1 && captured.offs==1 && captured.start==127 && captured.end==322,"host receives independent MIDI-only output with sample offsets");
            check(peak()<1e-6,"MIDI-only graph has silent audio");
            Playhead playhead;
            HostCallbackInfo callbacks{};
            callbacks.hostUserData=&playhead;
            callbacks.beatAndTempoProc=beatAndTempo;
            callbacks.transportStateProc=transport;
            requireOK(AudioUnitSetProperty(unit,kAudioUnitProperty_HostCallbacks,global,0,&callbacks,sizeof(callbacks)),"publish DAW tempo and transport");
            tri::Routing clip{};
            clip[tri::routeIndex(tri::RouteOsc1In)]=tri::ClipNode;
            clip[tri::routeIndex(tri::RouteOutputIn)]=tri::Osc1Node;
            requireOK(setRoutes(clip),"connect clip to output");
            requireOK(workflow->setAssets(assets),"load clip into playing AU");
            requireOK(AudioUnitReset(unit,global,0),"clear tails");
            set(tri::ClipRun,1); set(tri::ClipLoop,0);
            render(); check(peak()<1e-5,"clip waits for stopped host");
            playhead.playing=true;
            render(); check(peak()>.001,"clip starts at host beat zero");
            playhead.beat=8;
            set(tri::Release,.005);
            for(int i=0;i<3;++i) render();
            check(peak()<1e-5,"seek beyond one-shot clip silences it");
            playhead.beat=0;
            render(); check(peak()>.001,"DAW seek back restarts clip");
            HostCallbackInfo empty{};
            requireOK(AudioUnitSetProperty(unit,kAudioUnitProperty_HostCallbacks,global,0,&empty,sizeof(empty)),"remove host callbacks");
        }
        NSDictionary *instanceGraph=@{@"version":@3,
            @"nodes":@[
              @{@"id":@101,@"type":@(tri::DawMidiNode),@"x":@0,@"y":@0,@"parameters":@{}},
              @{@"id":@104,@"type":@(tri::ArrangerNode),@"x":@200,@"y":@0,@"parameters":@{}},
              @{@"id":@102,@"type":@(tri::Osc1Node),@"x":@400,@"y":@0,@"parameters":@{}},
              @{@"id":@103,@"type":@(tri::DawOutputNode),@"x":@600,@"y":@0,@"parameters":@{}}],
            @"edges":@[@{@"source":@101,@"target":@104,@"port":@0},
                         @{@"source":@104,@"sourcePort":@2,@"target":@102,@"port":@0},
                         @{@"source":@102,@"target":@103,@"port":@0}]};
        requireOK(workflow->setAssets(@{@"graph":instanceGraph}),"publish independent instance graph");
        finishHostTurn();
        check([workflow->readAssets()[@"graph"] isEqual:instanceGraph],"instance graph crosses AU boundary");
        requireOK(MusicDeviceMIDIEvent(unit,0x90,69,100,0),"play independent graph");
        render();check(peak()>.001,"arranged low voice reaches the oscillator and DAW audio output");
        requireOK(MusicDeviceMIDIEvent(unit,0x80,69,0,0),"release independent graph");
        if (argc>2) {
            AudioUnitCocoaViewInfo view{};
            bytes=sizeof(view);
            requireOK(AudioUnitGetProperty(unit,kAudioUnitProperty_CocoaUI,global,0,&view,&bytes),"Cocoa view property");
            NSURL* url=CFBridgingRelease(view.mCocoaAUViewBundleLocation);
            NSString* className=CFBridgingRelease(view.mCocoaAUViewClass[0]);
            NSBundle* uiBundle=[NSBundle bundleWithURL:url];
            check([uiBundle load],"view bundle loads");
            id<AUCocoaUIBase> maker=[[[uiBundle classNamed:className] alloc] init];
            check(maker!=nil,"editor factory");
            if (!strcmp(argv[2],"editor")) {
                [NSApplication sharedApplication];
                NSView* editor=[maker uiViewForAudioUnit:unit withSize:NSZeroSize];
                check(editor && editor.frame.size.width>400,"editor created");
            }
        }
        requireOK(AudioUnitUninitialize(unit),"final uninitialize");
        workflow.reset();
        requireOK(AudioComponentInstanceDispose(unit),"dispose");
        printf("PASS — Audio Unit, %d automated parameters, 1.0 and 1.1 migration, 1.2 routes/layout recall, live rewire, tail, MIDI offset, stereo sidechain, source isolation, embedded clips, host transport, and MIDI-only output offsets.\n",int(tri::ParamCount-tri::defaultRouting.size()));
    }
}
