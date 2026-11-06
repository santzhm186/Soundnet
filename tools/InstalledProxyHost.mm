#import <Cocoa/Cocoa.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreAudioKit/CoreAudioKit.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
static bool waitFor(const std::atomic_bool &finished, double seconds) {
    NSDate *end = [NSDate dateWithTimeIntervalSinceNow:seconds];
    while (!finished.load() && end.timeIntervalSinceNow>0)
        [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    return finished.load();
}
int main() {@autoreleasepool{
    [NSApplication sharedApplication];
    __block AVAudioUnit *unit=nil;
    std::atomic_bool finished{false}; auto *done=&finished;
    AudioComponentDescription desc={'aumu','Tern','Trny',0,0};
    [AVAudioUnit instantiateWithComponentDescription:desc options:kAudioComponentInstantiation_LoadOutOfProcess completionHandler:^(AVAudioUnit *au,NSError *error){
        if(error)fprintf(stderr,"instantiation error: %s\n",error.description.UTF8String);unit=au;done->store(true);
    }];
    if(!waitFor(finished,12) || !unit)return 2;
    printf("Loaded %s; version 0x%06lx; hosting out-of-process requested; class %s\n",unit.name.UTF8String,(unsigned long)unit.version,NSStringFromClass(unit.class).UTF8String);
    AVAudioUnitMIDIInstrument *instrument=(AVAudioUnitMIDIInstrument *)unit;
    if(![unit isKindOfClass:AVAudioUnitMIDIInstrument.class])return 3;
    AVAudioEngine *engine=[AVAudioEngine new];[engine attachNode:instrument];
    AVAudioFormat *fmt=[[AVAudioFormat alloc]initStandardFormatWithSampleRate:48000 channels:2];
    [engine connect:instrument to:engine.mainMixerNode format:fmt];
    NSError *error=nil;
    if(![engine enableManualRenderingMode:AVAudioEngineManualRenderingModeOffline format:fmt maximumFrameCount:256 error:&error] || ![engine startAndReturnError:&error]){
        fprintf(stderr,"graph start failed %s\n",error.description.UTF8String);return 4;}
    AVAudioPCMBuffer *buf=[[AVAudioPCMBuffer alloc]initWithPCMFormat:fmt frameCapacity:256];
    auto render=[&](int quanta){
        float peak=0;int successes=0;
        for(int n=0;n<quanta*4 && successes<quanta;n++){
            AVAudioEngineManualRenderingStatus status=[engine renderOffline:256 toBuffer:buf error:&error];
            if(status==AVAudioEngineManualRenderingStatusCannotDoInCurrentContext)continue;
            if(status!=AVAudioEngineManualRenderingStatusSuccess){fprintf(stderr,"render result %ld error=%s\n",(long)status,error.description.UTF8String);return -1.f;}
            for(unsigned ch=0;ch<2;ch++)for(unsigned j=0;j<buf.frameLength;j++)
                peak=std::max(peak,std::abs(buf.floatChannelData[ch][j]));
            ++successes;
        }
        return successes==quanta?peak:-1.f;
    };
    float quiet=render(4);[instrument startNote:69 withVelocity:110 onChannel:0];float note=render(160);
    [instrument stopNote:69 onChannel:0];float release=render(16);[engine stop];
    printf("Connected instrument output -> main mixer only; no sidechain. 48 kHz offline peaks: before %.6f, MIDI A4 %.6f, release %.6f\n",quiet,note,release);
    __block NSViewController *controller=nil;std::atomic_bool responded{false};auto *viewDone=&responded;
    [unit.AUAudioUnit requestViewControllerWithCompletionHandler:^(NSViewController *vc){controller=vc;viewDone->store(true);}];
    bool got=waitFor(responded,10);
    NSView *view=controller.view;
    if(view){
        NSWindow *window=[[NSWindow alloc]initWithContentRect:NSMakeRect(0,0,1144,650) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        window.releasedWhenClosed=NO;window.contentViewController=controller;[window display];
        [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.2]];
        printf("Editor controller %s, native view %s, frame %.0f x %.0f, attached to window: %s\n",NSStringFromClass(controller.class).UTF8String,NSStringFromClass(view.class).UTF8String,view.frame.size.width,view.frame.size.height,view.window==window?"yes":"no");
        [window close];
    }else printf("Editor request %s; controller nil\n",got?"completed":"timed out");
    return quiet>=0 && quiet<.0001 && note>.01f && view?0:5;
}}
