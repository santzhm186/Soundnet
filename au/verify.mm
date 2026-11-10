#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioToolbox/AUCocoaUIView.h>
#include "WorkflowEditorAccess.hpp"
#include "../platform/ProjectDocument.hpp"
#include "../platform/NodePackageLoader.h"
#include <CoreMIDI/CoreMIDI.h>
#include <dlfcn.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

static void check(bool ok,const char* label) {
    if(!ok){fprintf(stderr,"Failed: %s\n",label);exit(1);}
}
static void requireOK(OSStatus error,const char* label) {
    if(error){fprintf(stderr,"Failed: %s (%d)\n",label,int(error));exit(1);}
}
static OSStatus source(void* context,AudioUnitRenderActionFlags* flags,const AudioTimeStamp*,UInt32,UInt32 frames,AudioBufferList* data) {
    const bool audible=context!=nullptr;
    for(UInt32 channel=0;channel<data->mNumberBuffers;++channel)
        std::fill_n(static_cast<float*>(data->mBuffers[channel].mData),frames,audible?(channel?-.25f:.25f):0.f);
    if(!audible)*flags|=kAudioUnitRenderAction_OutputIsSilence;
    return noErr;
}
struct CapturedMIDI {unsigned ons=0,offs=0;MIDITimeStamp start=0,end=0;};
static OSStatus outputMIDI(void* context,const AudioTimeStamp* time,UInt32 port,const MIDIPacketList* list) {
    auto& capture=*static_cast<CapturedMIDI*>(context);
    if(port!=0||!time||!list||list->numPackets!=1)return kAudio_ParamError;
    const auto& event=list->packet[0];
    if(event.length==3&&event.data[1]==71) {
        if(event.data[0]==0x92&&event.data[2]){++capture.ons;capture.start=event.timeStamp;}
        if(event.data[0]==0x82){++capture.offs;capture.end=event.timeStamp;}
    }
    return noErr;
}
struct Playhead {double frame=0;bool playing=false;};
static OSStatus beatAndTempo(void* context,Float64* beat,Float64* tempo) {
    if(beat)*beat=static_cast<Playhead*>(context)->frame/24000.;
    if(tempo)*tempo=120;
    return noErr;
}
static OSStatus transport(void* context,Boolean* playing,Boolean*,Float64* frame,Boolean*,Float64*,Float64*) {
    auto& head=*static_cast<Playhead*>(context);if(playing)*playing=head.playing;if(frame)*frame=head.frame;
    return noErr;
}
static NSMutableDictionary* node(NSString* key,int identifier,NSDictionary* parameters=nil) {
    const auto* spec=tri::nodeSpecForKey(key.UTF8String);check(spec,"requested node loaded from component package");
    check(spec->type==tri::nodeTypeID(key.UTF8String)&&spec->sdkVersion==2&&!spec->legacyAdapter,"SDK 2 node identity");
    return [@{@"id":@(identifier),@"nodeKey":key,@"nodeVersion":@(spec->nodeVersion),@"stateVersion":@(spec->stateVersion),
        @"x":@(identifier*20),@"y":@0,@"parameters":parameters ?: @{}} mutableCopy];
}
static NSDictionary* edge(int from,NSString* output,int to,NSString* input) {
    return @{@"source":@(from),@"sourcePort":output,@"target":@(to),@"port":input};
}
static NSMutableDictionary* graph(NSArray* nodes,NSArray* edges) {
    NSMutableDictionary* result=tri::newProjectDocument();[result[@"nodes"] addObjectsFromArray:nodes];
    result[@"edges"]=[edges mutableCopy];return result;
}
static NSString* canonical(NSString* path) {
    return [[NSURL fileURLWithPath:path].URLByResolvingSymlinksInPath.path stringByStandardizingPath];
}
int main(int argc,char** argv) {@autoreleasepool {
    check(argc>=2&&strcmp(argv[1],"--installed-oop"),"usage: verify /absolute/Soundnet.component [editor]");
    NSString* bundlePath=canonical(@(argv[1]));
    NSDictionary* plist=[NSDictionary dictionaryWithContentsOfFile:[bundlePath stringByAppendingPathComponent:@"Contents/Info.plist"]];
    check(plist!=nil,"read the specified component Info.plist");
    const UInt32 version=[plist[@"AudioComponents"][0][@"version"] unsignedIntValue];
    check(version==0x30000,"specified component is Soundnet 3.0.0");
    NSString* binary=[bundlePath stringByAppendingPathComponent:@"Contents/MacOS/Soundnet"];
    NSString* platform=[bundlePath stringByAppendingPathComponent:@"Contents/Frameworks/libSoundnetPlatform.dylib"];
    NSString* packages=[bundlePath stringByAppendingPathComponent:@"Contents/Resources/Nodes"];
    // Isolate verification from installed components and user node overrides.
    setenv("SOUNDNET_NODE_PATH",packages.fileSystemRepresentation,1);
    setenv("SOUNDNET_NO_AUTOSAVE","1",1);
    Dl_info platformInfo{};
    check(dladdr(reinterpret_cast<void*>(&tri::nodeCatalogSnapshot),&platformInfo)&&platformInfo.dli_fname&&
          [canonical(@(platformInfo.dli_fname)) isEqual:canonical(platform)],"verifier links the specified component platform library");
    void* library=dlopen(binary.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL);
    if(!library){fprintf(stderr,"dlopen %s: %s\n",binary.fileSystemRepresentation,dlerror());return 1;}
    auto factory=reinterpret_cast<AudioComponentFactoryFunction>(dlsym(library,"TernaryUnitFactory"));
    check(factory,"exported AU factory");
    Dl_info factoryInfo{};
    check(dladdr(reinterpret_cast<void*>(factory),&factoryInfo)&&factoryInfo.dli_fname&&
          [canonical(@(factoryInfo.dli_fname)) isEqual:canonical(binary)],"factory belongs to the specified binary");
    AudioComponentDescription description={'aumu','Tern','Trny',0,0};
    AudioComponent component=AudioComponentRegister(&description,CFSTR("Soundnet: Direct build verification"),version,factory);
    check(component,"process-local component registration");
    UInt32 registeredVersion=0;requireOK(AudioComponentGetVersion(component,&registeredVersion),"registered version");
    check(registeredVersion==version,"registered version matches the component");
    AudioUnit unit=nullptr;requireOK(AudioComponentInstanceNew(component,&unit),"instantiate the direct factory");
    constexpr auto global=kAudioUnitScope_Global;UInt32 bytes=0;
    requireOK(AudioUnitGetPropertyInfo(unit,kAudioUnitProperty_ParameterList,global,0,&bytes,nullptr),"host parameter list");
    check(bytes&&bytes%sizeof(AudioUnitParameterID)==0,"valid host parameter list");
    std::vector<AudioUnitParameterID> parameters(bytes/sizeof(AudioUnitParameterID));
    requireOK(AudioUnitGetProperty(unit,kAudioUnitProperty_ParameterList,global,0,parameters.data(),&bytes),"read host parameters");
    for(auto id:parameters) {
        AudioUnitParameterInfo info{};bytes=sizeof(info);
        requireOK(AudioUnitGetProperty(unit,kAudioUnitProperty_ParameterInfo,global,id,&info,&bytes),"host parameter metadata");
        check(info.maxValue>=info.minValue&&info.name[0],"usable host parameter metadata");
        if(info.flags&kAudioUnitParameterFlag_CFNameRelease)CFRelease(info.cfNameString);
    }
    AudioStreamBasicDescription format{};format.mSampleRate=48000;format.mFormatID=kAudioFormatLinearPCM;
    format.mFormatFlags=kAudioFormatFlagsNativeFloatPacked|kAudioFormatFlagIsNonInterleaved;
    format.mBytesPerPacket=format.mBytesPerFrame=sizeof(float);format.mFramesPerPacket=1;format.mChannelsPerFrame=2;format.mBitsPerChannel=32;
    requireOK(AudioUnitSetProperty(unit,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&format,sizeof(format)),"sidechain format");
    requireOK(AudioUnitSetProperty(unit,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Output,0,&format,sizeof(format)),"output format");
    AURenderCallbackStruct input={source,nullptr};
    requireOK(AudioUnitSetProperty(unit,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&input,sizeof(input)),"silent sidechain");
    requireOK(AudioUnitInitialize(unit),"initialize SDK 2 package catalog");
    check(tri::nodeSpecForKey("soundnet.timecode")&&tri::nodeSpecForKey("soundnet.project-settings"),"platform environment packages loaded");
    auto workflow=std::make_unique<ternaryAU::EditorAccess>(unit);
    constexpr UInt32 frames=512;std::array<float,frames> left{},right{};
    struct Stereo {UInt32 count;AudioBuffer buffer[2];} buffers={2,{{1,frames*4,left.data()},{1,frames*4,right.data()}}};
    AudioTimeStamp time{};time.mFlags=kAudioTimeStampSampleTimeValid;
    auto render=[&] {AudioUnitRenderActionFlags flags=0;
        requireOK(AudioUnitRender(unit,&flags,&time,0,frames,reinterpret_cast<AudioBufferList*>(&buffers)),"render");
        time.mSampleTime+=frames;};
    auto peak=[&] {float result=0;for(float value:left){check(std::isfinite(value),"finite audio");result=std::max(result,std::abs(value));}return result;};
    auto publish=[&](NSDictionary* document) {requireOK(workflow->setAssets(@{@"graph":document}),"publish keyed instance graph");};
    auto processing=[&](bool enabled) {UInt32 value=enabled;requireOK(AudioUnitSetProperty(unit,ternaryAU::processing,global,0,&value,sizeof(value)),"processing control");};
    auto oscillator=[] {return node(@"soundnet.oscillator",102,@{@"osc1.wave":@0,@"envelope.attack":@.001,@"envelope.release":@.005});};
    auto output=[] {return node(@"soundnet.audio-output",103,@{@"output.master":@1,@"output.width":@1});};
    NSDictionary* synth=graph(@[node(@"soundnet.daw-midi-input",101),oscillator(),output()],
        @[edge(101,@"notes",102,@"notes"),edge(102,@"audio",103,@"audio")]);
    publish(synth);render();
    requireOK(workflow->sendLocalMIDI({0x90,69,110}),"local keyboard input");render();check(peak()<1e-6,"local input isolated from DAW node");
    requireOK(workflow->sendLocalMIDI({0x80,69,0}),"local key release");
    requireOK(MusicDeviceMIDIEvent(unit,0x90,60,100,128),"DAW onset at sample offset");render();
    check(std::all_of(left.begin(),left.begin()+128,[](float value){return std::abs(value)<1e-6;}),"silence before DAW MIDI offset");
    check(peak()>.001,"SDK oscillator sounds after DAW MIDI onset");
    requireOK(MusicDeviceMIDIEvent(unit,0x80,60,0,0),"DAW note release");render();
    NSMutableDictionary* invalid=[synth mutableCopy];invalid[@"edges"]=@[edge(101,@"notes",103,@"audio")];
    check(workflow->setAssets(@{@"graph":invalid})!=noErr&&[workflow->readAssets()[@"graph"] isEqual:synth],"invalid graph leaves the previous AU state intact");
    NSDictionary* rejectedCommand=@{@"version":@0,@"type":@((UInt32)'aumu'),@"subtype":@((UInt32)'Tern'),
        @"manufacturer":@((UInt32)'Trny'),@"name":@"Invalid edit",@"data":[NSData data],
        @"Ternary.EditorCommand":@(ternaryAU::editAssets),@"Soundnet.Assets.1":@{@"graph":invalid}};
    CFPropertyListRef rejected=(__bridge CFPropertyListRef)rejectedCommand;
    check(AudioUnitSetProperty(unit,kAudioUnitProperty_ClassInfo,global,0,&rejected,sizeof(rejected))!=noErr,
        "transient ClassInfo rejects invalid graph publication");
    workflow->invalidate();check([workflow->readAssets()[@"graph"] isEqual:synth],"failed transient edit keeps the previous graph");
    CapturedMIDI captured;AUMIDIOutputCallbackStruct delivery{outputMIDI,&captured};
    requireOK(AudioUnitSetProperty(unit,kAudioUnitProperty_MIDIOutputCallback,global,0,&delivery,sizeof(delivery)),"host MIDI callback");
    publish(graph(@[node(@"soundnet.daw-midi-input",101),node(@"soundnet.midi-output",104)],@[edge(101,@"notes",104,@"notes")]));
    render();
    requireOK(MusicDeviceMIDIEvent(unit,0x92,71,100,127),"MIDI-only note on");
    requireOK(MusicDeviceMIDIEvent(unit,0x82,71,0,322),"MIDI-only note off");render();
    check(captured.ons==1&&captured.offs==1&&captured.start==127&&captured.end==322,"MIDI channel and exact host buffer offsets");
    check(peak()<1e-6,"MIDI-only graph has silent audio");
    requireOK(AudioUnitUninitialize(unit),"uninitialize before connecting sidechain");
    input.inputProcRefCon=&input;
    requireOK(AudioUnitSetProperty(unit,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&input,sizeof(input)),"stereo sidechain");
    requireOK(AudioUnitInitialize(unit),"reinitialize with stereo sidechain");
    NSDictionary* audio=graph(@[node(@"soundnet.audio-input",101,@{@"input.gain":@.5}),output()],@[edge(101,@"audio",103,@"audio")]);
    publish(audio);render();
    check(std::abs(left[450]-.125f)<1e-6&&std::abs(right[450]+.125f)<1e-6,"SDK audio I/O passes both sidechain channels at node gain");
    CFPropertyListRef preset=nullptr;bytes=sizeof(preset);
    requireOK(AudioUnitGetProperty(unit,kAudioUnitProperty_ClassInfo,global,0,&preset,&bytes),"save AU ClassInfo");
    publish(graph(@[],@[]));render();check(peak()<1e-6,"live disconnect silences output");
    requireOK(AudioUnitSetProperty(unit,kAudioUnitProperty_ClassInfo,global,0,&preset,sizeof(preset)),"restore AU ClassInfo");
    CFRelease(preset);workflow->invalidate();
    check([workflow->readAssets()[@"graph"] isEqual:audio],"modern graph survives AU preset round trip");
    render();check(std::abs(left[450]-.125f)<1e-6,"restored graph resumes sidechain audio");
    processing(false);render();check(peak()<1e-6,"stop silences processing");
    processing(true);render();check(std::abs(left[450]-.125f)<1e-6,"resume restores processing");
    requireOK(workflow->refreshNodes(),"refresh through transient ClassInfo command");
    check([workflow->readAssets()[@"graph"] isEqual:audio],"refresh command preserves graph state");
    CFPropertyListRef afterRefresh=nullptr;bytes=sizeof(afterRefresh);
    requireOK(AudioUnitGetProperty(unit,kAudioUnitProperty_ClassInfo,global,0,&afterRefresh,&bytes),"state after transient refresh");
    NSDictionary* refreshedState=CFBridgingRelease(afterRefresh);
    check(!refreshedState[@"Ternary.EditorCommand"]&&[refreshedState[@"Soundnet.Assets.1"][@"graph"] isEqual:audio],
        "refresh command is not persisted as a replacement preset");
    render();check(peak()<1e-6,"refresh stops processing");processing(true);
    render();check(std::abs(left[450]-.125f)<1e-6,"refreshed graph resumes without host rebuild");
    const unsigned char smf[]={'M','T','h','d',0,0,0,6,0,0,0,1,0,96,'M','T','r','k',0,0,0,12,0,0x90,69,110,96,0x80,69,0,0,0xff,0x2f,0};
    auto clip=node(@"soundnet.midi-clip",101,@{@"clip.run":@1,@"clip.loop":@0,@"clock.bpm":@120});
    clip[@"asset"]=@{@"name":@"embedded.mid",@"smf":[[NSData dataWithBytes:smf length:sizeof(smf)]base64EncodedStringWithOptions:0]};
    Playhead playhead;HostCallbackInfo callbacks{};callbacks.hostUserData=&playhead;callbacks.beatAndTempoProc=beatAndTempo;callbacks.transportStateProc=transport;
    requireOK(AudioUnitSetProperty(unit,kAudioUnitProperty_HostCallbacks,global,0,&callbacks,sizeof(callbacks)),"host transport callbacks");
    publish(graph(@[clip,oscillator(),output()],@[edge(101,@"notes",102,@"notes"),edge(102,@"audio",103,@"audio")]));
    render();check(peak()<1e-6,"MIDI clip waits while host transport is stopped");
    playhead.playing=true;render();check(peak()>.001,"MIDI clip starts with host transport");
    playhead.frame=96000;render();render();check(peak()<1e-5,"seek past one-shot clip releases sound");
    playhead.frame=0;render();check(peak()>.001,"seek back restarts one-shot clip");
    callbacks={};requireOK(AudioUnitSetProperty(unit,kAudioUnitProperty_HostCallbacks,global,0,&callbacks,sizeof(callbacks)),"remove transport callbacks");
    if(argc>2&&!strcmp(argv[2],"editor")) {
        [NSApplication sharedApplication];AudioUnitCocoaViewInfo view{};bytes=sizeof(view);
        requireOK(AudioUnitGetProperty(unit,kAudioUnitProperty_CocoaUI,global,0,&view,&bytes),"Cocoa editor property");
        NSURL* url=CFBridgingRelease(view.mCocoaAUViewBundleLocation);NSString* name=CFBridgingRelease(view.mCocoaAUViewClass[0]);
        check([canonical(url.path) hasPrefix:[bundlePath stringByAppendingString:@"/"]],"editor belongs to specified component");
        NSBundle* ui=[NSBundle bundleWithURL:url];check([ui load],"load specified editor bundle");
        id<AUCocoaUIBase> maker=[[[ui classNamed:name]alloc]init];check(maker!=nil,"editor factory");
        NSView* editor=[maker uiViewForAudioUnit:unit withSize:NSZeroSize];check(editor&&editor.frame.size.width>400,"create modern AU editor");
    }
    requireOK(AudioUnitUninitialize(unit),"final uninitialize");workflow.reset();
    requireOK(AudioComponentInstanceDispose(unit),"dispose direct AU");
    printf("PASS: exact component %s; SDK 2 graphs, MIDI offsets/isolation, stereo sidechain, preset state, refresh, transport%s\n",
        bundlePath.fileSystemRepresentation,argc>2?" and editor":"");
}}
