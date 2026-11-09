#import "../ui/TriEditor.h"
#import "../src/WorkflowAssets.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>

@interface NSView (SoundnetProcessingActions)
- (BOOL)adoptFile:(NSDictionary *)file;
- (void)runExport:(NSInteger)kind toURL:(NSURL *)url seconds:(double)seconds rate:(double)rate bits:(int)bits;
- (void)cancelProcessing:(id)sender;
- (void)stopGraph:(id)sender;
- (void)playGraph:(id)sender;
- (void)clipAudition:(id)sender;
- (void)clipPreview:(id)sender;
- (void)press:(int)note;
@end

static NSArray<NSString *> *stagingDirectories(NSURL *folder) {
    NSArray *names=[[NSFileManager defaultManager] contentsOfDirectoryAtPath:folder.path error:nil];
    return [names filteredArrayUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(NSString *name,NSDictionary *) {
        return [name hasPrefix:@".soundnet-export-"];
    }]];
}
static void waitForExport(NSView *editor) {
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:20];
    while([[editor valueForKey:@"exportBusy"] boolValue] && deadline.timeIntervalSinceNow>0) {
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    }
    assert(![[editor valueForKey:@"exportBusy"] boolValue]);
    assert([[editor valueForKey:@"cancelButton"] isHidden]);
    assert([[editor valueForKey:@"exportProgress"] isHidden]);
}
static void waitForStaging(NSURL *folder) {
    // The worker creates staging before rendering. Keep the main queue held so
    // even a very fast worker cannot publish before this test requests Cancel.
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!stagingDirectories(folder).count && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(stagingDirectories(folder).count==1);
}
int main() { setenv("SOUNDNET_NO_AUTOSAVE","1",1); @autoreleasepool {
    [NSApplication sharedApplication];
    __block NSDictionary *assets=@{};
    __block BOOL processing=YES;
    NSMutableArray<NSArray<NSNumber *> *> *midiEvents=[NSMutableArray new];
    NSView *editor=TRICreateEditor(
        ^float(int p){return tri::params[p].def;},^(int p,float value){},
        ^(uint8_t status,uint8_t note,uint8_t velocity){[midiEvents addObject:@[@(status),@(note),@(velocity)]];},
        ^BOOL{return NO;},^(BOOL){},^NSString *{return @"PROCESSING TEST";},
        ^NSDictionary *{return assets;},^BOOL(NSDictionary *replacement){assets=replacement;return YES;},
        ^BOOL{return processing;},^(BOOL enabled){processing=enabled;});
    NSWindow *window=[[NSWindow alloc]initWithContentRect:editor.frame
        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    window.releasedWhenClosed=NO;window.contentView=editor;
    NSDictionary *fixture=[NSJSONSerialization JSONObjectWithData:
        [NSData dataWithContentsOfFile:@"examples/three-voice-arrangement.v4.json"] options:0 error:nil];
    assert(fixture && [editor adoptFile:fixture]);
    NSURL *folder=[[NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES]
        URLByAppendingPathComponent:[@"Soundnet-ProcessingUITests-" stringByAppendingString:NSUUID.UUID.UUIDString] isDirectory:YES];
    assert([[NSFileManager defaultManager] createDirectoryAtURL:folder withIntermediateDirectories:YES attributes:nil error:nil]);
    NSData *sentinel=[@"existing export must survive cancellation\n" dataUsingEncoding:NSUTF8StringEncoding];
    for(NSInteger kind=0;kind<3;++kind) {
        NSURL *target=kind==2?folder:[folder URLByAppendingPathComponent:kind==0?@"existing.mid":@"existing.wav"];
        NSURL *existing=kind==2?[folder URLByAppendingPathComponent:@"stem-8.wav"]:target;
        assert([sentinel writeToURL:existing atomically:YES]);
        [editor runExport:kind toURL:target seconds:120 rate:48000 bits:16];
        assert([[editor valueForKey:@"exportBusy"] boolValue]);
        assert(![[editor valueForKey:@"cancelButton"] isHidden]);
        waitForStaging(folder);
        if(kind==1) { [editor stopGraph:nil];assert(!processing); }
        else [editor cancelProcessing:nil];
        // Resuming live playback must never clear the cancelled export token.
        [editor playGraph:nil];assert(processing);
        waitForExport(editor);
        assert([[NSData dataWithContentsOfURL:existing] isEqual:sentinel]);
        assert(stagingDirectories(folder).count==0);
    }
    // Completion returns the controls to idle and a following export can save.
    NSURL *success=[folder URLByAppendingPathComponent:@"completed.wav"];
    [editor runExport:1 toURL:success seconds:.1 rate:48000 bits:16];
    waitForExport(editor);
    NSData *wav=[NSData dataWithContentsOfURL:success];
    assert(wav.length>44 && std::memcmp(wav.bytes,"RIFF",4)==0);
    assert(stagingDirectories(folder).count==0);
    // A write failure also clears Busy and does not leave staging artifacts.
    NSURL *missing=[[folder URLByAppendingPathComponent:@"missing" isDirectory:YES] URLByAppendingPathComponent:@"failed.wav"];
    [editor runExport:1 toURL:missing seconds:.1 rate:48000 bits:16];
    waitForExport(editor);
    assert(![[NSFileManager defaultManager] fileExistsAtPath:missing.path]);
    assert(stagingDirectories(folder).count==0);
    // Stop invalidates delayed audition callbacks, including old Note Offs
    // that could otherwise cut a new performance after Play.
    NSView *roll=[[NSClassFromString(@"SNNoteRollView") alloc]initWithFrame:NSMakeRect(0,0,500,300)];
    [roll setValue:@[[ @{ @"pitch":@60,@"start":@0.2,@"length":@0.1,@"velocity":@100 } mutableCopy]] forKey:@"notes"];
    [roll setValue:@0 forKey:@"selectedIndex"];
    [editor setValue:roll forKey:@"clipRoll"];
    for(NSInteger mode=0;mode<2;++mode) {
        if(mode==0)[editor clipAudition:nil];else [editor clipPreview:nil];
        [editor stopGraph:nil];[editor playGraph:nil];[midiEvents removeAllObjects];
        NSDate *until=[NSDate dateWithTimeIntervalSinceNow:.4];
        while(until.timeIntervalSinceNow>0)
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
        assert(midiEvents.count==0);
    }
    NSView *piano=[editor valueForKey:@"piano"];
    [piano press:60];assert([[piano valueForKey:@"active"] intValue]==60);
    [editor stopGraph:nil];assert([[piano valueForKey:@"active"] intValue]==0);
    [editor playGraph:nil];[midiEvents removeAllObjects];[piano press:60];
    assert(midiEvents.count==1 && [midiEvents[0][0] intValue]==0x90);
    [editor stopGraph:nil];
    [window close];
    assert([[NSFileManager defaultManager] removeItemAtURL:folder error:nil]);
    puts("PASS: native export cancellation preserves existing MIDI/WAV/stems, removes staging, resets Busy, survives live resume, permits another export, and cancels audition/piano state.");
} }
