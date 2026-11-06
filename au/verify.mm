#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioToolbox/AUCocoaUIView.h>
#include "WorkflowEditorAccess.hpp"
#include <memory>
#include <dlfcn.h>
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
            check(version==0x10300,"system found installed 1.2");
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
            auto comp=AudioComponentRegister(&desc,CFSTR("Ternary: Ternary"),0x10300,factory);
            check(comp,"component registration");
            requireOK(AudioComponentInstanceNew(comp,&unit),"AudioComponentInstanceNew");
        }
        auto global=kAudioUnitScope_Global;
        UInt32 bytes=0;
        requireOK(AudioUnitGetPropertyInfo(unit,kAudioUnitProperty_ParameterList,global,0,&bytes,nullptr),"parameter list");
        check(bytes==sizeof(UInt32)*(tri::ParamCount-(tri::RouteVelocityClock-tri::RouteArpIn+1)),"complete parameter list");
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
        requireOK(setRoutes(newGraph),"rewire ARP bypass and audio output");
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
        printf("PASS — Audio Unit, %d automated parameters, 1.0 and 1.1 migration, 1.2 routes/layout recall, live rewire, tail, MIDI offset and stereo sidechain.\n",tri::RouteArpIn);
    }
}
