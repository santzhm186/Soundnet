#import "../ui/TriEditor.h"
#import "../ui/SNLocalization.h"
#import "../src/WorkflowAssets.h"
#include "../src/Engine.hpp"
#include "../src/WavFile.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <functional>
#include <memory>
#include <cassert>
#include <unistd.h>

@interface NSView (SoundnetTestActions)
- (void)insert:(NSMenuItem *)item;
- (void)undo:(id)sender;
- (void)redo:(id)sender;
- (void)commit:(NSDictionary *)previous;
- (void)fit:(id)sender;
- (void)inspect;
- (void)selectAll:(id)sender;
- (void)copy:(id)sender;
- (void)paste:(id)sender;
- (void)remove:(id)sender;
- (NSArray *)activeIDs;
- (BOOL)dropFileURLs:(NSArray<NSURL *> *)urls atPoint:(NSPoint)point clipMode:(BOOL)clipMode;
- (BOOL)adoptFile:(NSDictionary *)file;
- (NSDictionary *)migrate:(NSDictionary *)file;
- (void)sliderChanged:(NSSlider *)slider;
- (void)choiceChanged:(NSPopUpButton *)popup;
- (void)nameChanged:(NSTextField *)field;
- (void)bypassChanged:(NSButton *)button;
- (void)selectOnly:(uint64_t)uid;
- (void)stopGraph:(id)sender;
- (void)playGraph:(id)sender;
- (void)trigger:(id)sender;
- (void)languageChanged:(NSPopUpButton *)sender;
- (void)toggleParameterGroup:(NSButton *)sender;
- (void)toggleLibrary:(id)sender;
- (void)cancelProcessing:(id)sender;
- (void)importScore:(id)sender;
- (void)selectOnly:(uint64_t)uid;
@end

static NSDictionary *snapshot(NSView *editor) {
    NSData *data=[NSJSONSerialization dataWithJSONObject:[editor valueForKey:@"document"] options:0 error:nil];
    return [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
}

static void savePreview(NSView *editor,NSString *path) {
    [editor.window display];
    NSBitmapImageRep *image=[editor bitmapImageRepForCachingDisplayInRect:editor.bounds];
    [editor cacheDisplayInRect:editor.bounds toBitmapImageRep:image];
    assert([[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES]);
}

static void waitForJob(NSView *editor) {
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:10];
    while([[editor valueForKey:@"exportBusy"] boolValue] && deadline.timeIntervalSinceNow>0)
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    assert(![[editor valueForKey:@"exportBusy"] boolValue]);
}

int main() {
    @autoreleasepool {
        BOOL ownPreferences=!getenv("SOUNDNET_PREFERENCES_SUITE");
        NSString *preferenceSuite=ownPreferences?[NSString stringWithFormat:@"com.soundnet.editor-tests.%d",getpid()]:@(getenv("SOUNDNET_PREFERENCES_SUITE"));
        if(ownPreferences)setenv("SOUNDNET_PREFERENCES_SUITE",preferenceSuite.UTF8String,1);
        [NSApplication sharedApplication];
        id originalLanguage=[SNLanguagePreferences() objectForKey:@"SoundnetLanguage"];
        BOOL originalEnglish=SNIsEnglish();
        SNSetEnglish(NO);
        auto engine = std::make_shared<tri::Engine>();
        __block NSDictionary *assets = @{};
        NSView *editor = TRICreateEditor(
            ^float(int p) { return engine->get(p); },
            ^(int p, float value) { engine->set(p, value); },
            ^(uint8_t s, uint8_t a, uint8_t b) { engine->midi(s,a,b); },
            ^BOOL { return NO; }, ^(BOOL) {}, ^NSString *{ return @"TEST"; },
            ^NSDictionary *{ return assets; },
            ^BOOL(NSDictionary *replacement) {
                tri::GraphSpec graph;
                if (!triassets::decodeGraph(replacement[@"graph"],graph,nullptr)) return NO;
                if (!engine->setGraph(graph)) return NO;
                assets = replacement;
                return YES;
            },
            ^BOOL { return engine->processingEnabled(); },
            ^(BOOL enabled) { engine->setProcessingEnabled(enabled); });
        NSDictionary *initialDocument=snapshot(editor);
        const NSUInteger initialNodeCount=[initialDocument[@"nodes"] count];
        assert(initialNodeCount==2);
        assert([initialDocument[@"settings"][@"environmentRequired"] boolValue]);
        NSSet *environmentKeys=[NSSet setWithArray:[initialDocument[@"nodes"] valueForKey:@"nodeKey"]];
        assert(([environmentKeys isEqualToSet:[NSSet setWithArray:@[@"soundnet.timecode",@"soundnet.project-settings"]]]));
        for (int i=0; i<25; ++i) {
            NSMenuItem *item = [NSMenuItem new];
            item.tag = tri::Osc1Node;
            [editor insert:item];
        }
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+25);
        NSMenuItem *start = [NSMenuItem new];
        start.tag = tri::StartNode;
        [editor insert:start];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+26);
        [editor undo:nil];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+25);
        [editor redo:nil];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+26);
        [editor selectAll:nil];
        assert([[editor activeIDs] count]==initialNodeCount+26);
        // Deleting all ordinary nodes retains the exact project environment;
        // undo still restores every user node as one document transaction.
        NSDictionary *beforeDeleteAll=snapshot(editor);
        [editor remove:nil];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount);
        assert([assets[@"graph"][@"nodes"] isEqual:initialDocument[@"nodes"]]);
        [editor undo:nil];assert([snapshot(editor) isEqual:beforeDeleteAll]);
        [editor selectAll:nil];
        [editor copy:nil];
        [editor paste:nil];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+52);
        assert([[editor activeIDs] count]==26);
        [editor remove:nil];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+26);
        [editor undo:nil];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+52);
        [editor redo:nil];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+26);
        NSMenuItem *sampler = [NSMenuItem new];
        sampler.tag = tri::SamplerNode;
        [editor insert:sampler];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+27);
        NSString *samplerKey=[NSString stringWithUTF8String:tri::nodeKey(tri::SamplerNode)];
        NSDictionary *samplerNode = [[assets[@"graph"][@"nodes"] filteredArrayUsingPredicate:
            [NSPredicate predicateWithFormat:@"nodeKey == %@", samplerKey]] firstObject];
        assert(samplerNode && [samplerNode[@"parameters"] count]==10);
        std::vector<float> wave(4800), waveR(4800);
        for (size_t i=0; i<wave.size(); ++i) wave[i]=waveR[i]=0.2f*std::sin(2*M_PI*440*i/48000.0);
        std::string why;
        const std::string dropPath = "/tmp/soundnet-drop-test.wav";
        assert(tri::wav::write(dropPath, wave, waveR, 48000, why));
        NSURL *dropURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:dropPath.c_str()]];
        assert([editor dropFileURLs:@[dropURL] atPoint:NSMakePoint(100, 100) clipMode:NO]);
        waitForJob(editor);
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+28);
        NSDictionary *samplerDrop = [assets[@"graph"][@"nodes"] lastObject];
        assert(samplerDrop && [samplerDrop[@"nodeKey"] isEqual:samplerKey] && samplerDrop[@"asset"][@"name"]);
        assert([samplerDrop[@"x"] doubleValue]==100 && [samplerDrop[@"y"] doubleValue]==100);
        assert([editor dropFileURLs:@[dropURL] atPoint:NSMakePoint(140, 140) clipMode:YES]);
        waitForJob(editor);
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+29);
        NSDictionary *clipDrop = [assets[@"graph"][@"nodes"] lastObject];
        NSString *clipKey=[NSString stringWithUTF8String:tri::nodeKey(tri::AudioClipNode)];
        assert(clipDrop && [clipDrop[@"nodeKey"] isEqual:clipKey] && clipDrop[@"asset"][@"name"]);
        assert([clipDrop[@"x"] doubleValue]==140 && [clipDrop[@"y"] doubleValue]==140);

        // A drop is accepted before decoding. Cancellation must discard every
        // file in the batch, and an outstanding job must reject another drop.
        NSDictionary *beforeCancelledDrop=snapshot(editor);
        assert(([editor dropFileURLs:@[dropURL,dropURL] atPoint:NSMakePoint(180,180) clipMode:NO]));
        assert([[editor valueForKey:@"exportBusy"] boolValue]);
        assert(![editor dropFileURLs:@[dropURL] atPoint:NSMakePoint(200,200) clipMode:NO]);
        [editor cancelProcessing:nil];
        waitForJob(editor);
        assert([snapshot(editor) isEqual:beforeCancelledDrop]);

        // Legacy routes skip retired clocks and use the current I/O defaults.
        NSMutableDictionary *legacyParams=[NSMutableDictionary new];
        for(int pid=0;pid<tri::ParamCount;++pid)
            if(!tri::routeParam(pid))legacyParams[@(tri::params[pid].name)]=@(tri::params[pid].def);
        NSDictionary *converted=[editor migrate:@{@"format":@"audio.ternary.workflow",@"version":@2,
            @"parameters":legacyParams,@"nodes":@{@"input":@[@0,@0],@"output":@[@200,@0],
                @"clock":@[@0,@200],@"velocity":@[@200,@200]},
            @"connections":@{@"output.audio":@"input",@"velocity.clock":@"clock"}}];
        tri::GraphSpec legacyGraph;
        assert(triassets::decodeGraph(converted,legacyGraph,nullptr));
        assert(legacyGraph.nodes.size()==3 && legacyGraph.edges.size()==1);
        for(const auto& node:legacyGraph.nodes)if(node.type==tri::AudioOutputNode)
            assert(node.parameters.at(tri::OutputWidth)==1);

        // Opening a v4 project must go through the canonicaliser, not the v1/v2
        // migrator.
        NSDictionary *v4=[NSJSONSerialization JSONObjectWithData:
            [NSData dataWithContentsOfFile:@"examples/three-voice-arrangement.v4.json"]
            options:NSJSONReadingMutableContainers error:nil];
        // A completed decode belongs to the document the user dropped into,
        // even if the user opens a different workflow before it finishes.
        assert([editor dropFileURLs:@[dropURL] atPoint:NSMakePoint(180,180) clipMode:NO]);
        assert([editor adoptFile:v4]);
        NSDictionary *afterOpen=snapshot(editor);
        waitForJob(editor);
        assert([snapshot(editor) isEqual:afterOpen]);
        std::remove(dropPath.c_str());
        NSDictionary *opened=[editor valueForKey:@"document"];
        assert([opened[@"version"] isEqual:@4]);
        assert([opened[@"nodes"] count]==7);
        assert([opened[@"nodes"][1][@"nodeKey"] isEqual:@"soundnet.voice-arranger"]);
        assert([opened[@"edges"][1][@"sourcePort"] isEqual:@"high"]);

        // Inspector editing: every wired action must exist and reach the
        // document. These three regressions (dead enum popup, dead name field,
        // slider rounding 0-1 values) were all silent in the compiled build.
        {
            NSDictionary *doc=assets[@"graph"];
            NSUInteger oscIndex=0;
            for(NSUInteger i=0;i<[doc[@"nodes"] count];++i)
                if([doc[@"nodes"][i][@"nodeKey"] isEqual:@"soundnet.oscillator"]){oscIndex=i;break;}
            uint64_t uid=[doc[@"nodes"][oscIndex][@"id"] unsignedLongLongValue];
            [editor setValue:@(uid) forKey:@"selected"];
            [editor inspect];
            NSView *inspector=[editor valueForKey:@"inspector"];
            std::function<id(NSView*,Class,int)> find;
            find=[&](NSView *root,Class cls,int tag)->id{
                for(NSView *v in root.subviews) {
                    if([v isKindOfClass:cls]&&(tag<0||v.tag==tag))return v;
                    id deep=find(v,cls,tag);
                    if(deep)return deep;
                }
                return nil;
            };
            NSSlider *level=(NSSlider *)find(inspector,[NSSlider class],tri::Level1+100000);
            assert(level);
            level.doubleValue=0.42;
            [editor sliderChanged:level];
            double stored=[assets[@"graph"][@"nodes"][oscIndex][@"parameters"][@"osc1.level"] doubleValue];
            assert(std::abs(stored-0.42)<1e-5);
            NSPopUpButton *wave=(NSPopUpButton *)find(inspector,[NSPopUpButton class],tri::Wave1);
            assert(wave && wave.numberOfItems==4);
            [wave selectItemAtIndex:1];
            [editor choiceChanged:wave];
            assert([assets[@"graph"][@"nodes"][oscIndex][@"parameters"][@"osc1.wave"] intValue]==1);
            NSTextField *name=(NSTextField *)find(inspector,[NSTextField class],990001);
            assert(name);
            name.stringValue=@"Lead";
            [editor nameChanged:name];
            assert([assets[@"graph"][@"nodes"][oscIndex][@"name"] isEqual:@"Lead"]);

            // Disclosure controls change presentation only. Rebuilt controls
            // must retain and continue editing the same stable parameter IDs.
            NSDictionary *beforeGroups=snapshot(editor);
            NSButton *mix=(NSButton *)find(inspector,[NSButton class],990200+int(tri::ParamGroup::Mix));
            assert(mix && mix.action==@selector(toggleParameterGroup:));
            [editor toggleParameterGroup:mix];
            assert(!find(inspector,[NSSlider class],tri::Level1+100000));
            assert([snapshot(editor) isEqual:beforeGroups]);
            mix=(NSButton *)find(inspector,[NSButton class],990200+int(tri::ParamGroup::Mix));
            [editor toggleParameterGroup:mix];
            level=(NSSlider *)find(inspector,[NSSlider class],tri::Level1+100000);
            assert(level && std::abs(level.doubleValue-.42)<1e-5);
            level.doubleValue=.37;[editor sliderChanged:level];
            assert(std::abs([assets[@"graph"][@"nodes"][oscIndex][@"parameters"][@"osc1.level"] doubleValue]-.37)<1e-5);
            NSButton *tone=(NSButton *)find(inspector,[NSButton class],990200+int(tri::ParamGroup::Tone));
            [editor toggleParameterGroup:tone];
            assert(!find(inspector,[NSPopUpButton class],tri::Wave1));
            tone=(NSButton *)find(inspector,[NSButton class],990200+int(tri::ParamGroup::Tone));
            [editor toggleParameterGroup:tone];
            wave=(NSPopUpButton *)find(inspector,[NSPopUpButton class],tri::Wave1);
            assert(wave && wave.indexOfSelectedItem==1);
            [wave selectItemAtIndex:2];[editor choiceChanged:wave];
            assert([assets[@"graph"][@"nodes"][oscIndex][@"parameters"][@"osc1.wave"] intValue]==2);

            // Use a custom name that also exists in the translation dictionary.
            // It remains user data in both UI languages and in the document.
            name=(NSTextField *)find(inspector,[NSTextField class],990001);
            name.stringValue=@"振荡器";[editor nameChanged:name];
            NSDictionary *beforeLanguage=snapshot(editor);
            NSPopUpButton *language=[editor valueForKey:@"languagePopup"];
            [language selectItemAtIndex:1];[editor languageChanged:language];
            assert(SNIsEnglish());
            assert([((NSButton *)[editor valueForKey:@"playButton"]).title isEqualToString:@"Play"]);
            assert([snapshot(editor) isEqual:beforeLanguage]);
            name=(NSTextField *)find(inspector,[NSTextField class],990001);
            assert([name.stringValue isEqualToString:@"振荡器"]);
            wave=(NSPopUpButton *)find(inspector,[NSPopUpButton class],tri::Wave1);
            assert(wave.indexOfSelectedItem==2 && wave.numberOfItems==4);
            [language selectItemAtIndex:0];[editor languageChanged:language];
            assert(!SNIsEnglish());
            assert([((NSButton *)[editor valueForKey:@"playButton"]).title isEqualToString:@"播放"]);
            assert([snapshot(editor) isEqual:beforeLanguage]);

            NSView *canvas=[editor valueForKey:@"canvas"];
            NSRect originalCanvas=canvas.frame;
            [editor toggleLibrary:nil];
            assert(((NSScrollView *)[editor valueForKey:@"libraryScroll"]).hidden);
            assert(NSWidth(canvas.frame)>NSWidth(originalCanvas)+200);
            [editor toggleLibrary:nil];
            assert(!((NSScrollView *)[editor valueForKey:@"libraryScroll"]).hidden);
            assert(NSEqualRects(originalCanvas,canvas.frame));
            assert([snapshot(editor) isEqual:beforeLanguage]);

            NSDictionary *beforeStop=snapshot(editor);
            [editor stopGraph:nil];
            assert(!engine->processingEnabled());
            assert(![[editor valueForKey:@"processingActive"] boolValue]);
            assert([snapshot(editor) isEqual:beforeStop]);
            [editor playGraph:nil];
            assert(engine->processingEnabled());
            [editor insert:start];
            [editor stopGraph:nil];
            uint64_t startID=0;
            for(NSDictionary *node in assets[@"graph"][@"nodes"])
                if([node[@"nodeKey"] isEqual:@"soundnet.start"])startID=[node[@"id"] unsignedLongLongValue];
            assert(startID);
            [editor selectOnly:startID];[editor trigger:nil];
            assert(engine->processingEnabled());
        }

        // ---- score loop: an ABC score becomes an editable MIDI clip --------
        {
            NSMutableDictionary *document=[@{@"version":@4,@"projectId":@"local",
                @"nodes":[NSMutableArray new],@"edges":[NSMutableArray new]} mutableCopy];
            [editor setValue:document forKey:@"document"];
            NSMenuItem *aiItem=[NSMenuItem new];aiItem.tag=tri::nodeTypeForKey("soundnet.yue2");
            [editor insert:aiItem];
            NSMutableArray *nodes=[editor valueForKey:@"document"][@"nodes"];
            assert(nodes.count==1);
            NSMutableDictionary *aiNode=nodes.lastObject;
            uint64_t aiID=[aiNode[@"id"] unsignedLongLongValue];
            aiNode[@"texts"]=@{@"yue2.style":@"smoke test",
                               @"yue2.score":@"X:1\nT:Smoke\nM:4/4\nL:1/4\nQ:1/4=110\nK:G\nG A B c | d2 B2 |\n"};
            [editor selectOnly:aiID];
            [editor commit:nil];
            assert([assets[@"graph"][@"nodes"] count]==1);
            [editor importScore:nil];
            nodes=[editor valueForKey:@"document"][@"nodes"];
            assert(nodes.count==2);
            NSDictionary *scoreClip=nodes.lastObject;
            assert([scoreClip[@"nodeKey"] isEqual:@"soundnet.midi-clip"]);
            assert([scoreClip[@"asset"][@"notes"] intValue]==6);
            assert([scoreClip[@"asset"][@"smf"] length]>0);
            assert([scoreClip[@"texts"][@"clip.score"] length]>0);
            assert([[editor activeIDs] count]==1);
            [editor undo:nil];
            assert([(NSArray *)[editor valueForKey:@"document"][@"nodes"] count]==1);
            [editor redo:nil];
            assert([(NSArray *)[editor valueForKey:@"document"][@"nodes"] count]==2);
        }

        NSData *serialized = [NSJSONSerialization dataWithJSONObject:assets options:0 error:nil];
        assert(triassets::validate([NSJSONSerialization JSONObjectWithData:serialized options:0 error:nil],nullptr));

        NSDictionary *example = [NSJSONSerialization JSONObjectWithData:
            [NSData dataWithContentsOfFile:@"examples/three-loops-then-wait.json"]
            options:NSJSONReadingMutableContainers error:nil];
        tri::GraphSpec decoded;
        assert(triassets::decodeGraph(example,decoded,nullptr));
        NSWindow *window = [[NSWindow alloc] initWithContentRect:editor.frame
            styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        window.releasedWhenClosed = NO;
        window.contentView = editor;
        [editor setValue:triassets::canonicalGraph(example,nil) forKey:@"document"];
        uint64_t previewID=0;
        for(NSDictionary *node in [editor valueForKey:@"document"][@"nodes"])
            if([node[@"nodeKey"] isEqual:@"soundnet.oscillator"]){previewID=[node[@"id"] unsignedLongLongValue];break;}
        assert(previewID);
        [editor selectOnly:previewID];
        [editor commit:nil];
        [editor fit:nil];
        [editor inspect];
        [[NSFileManager defaultManager] createDirectoryAtPath:@"build/qa" withIntermediateDirectories:YES attributes:nil error:nil];
        savePreview(editor,@"build/qa/instances.png");
        savePreview(editor,@"build/qa/instances-zh.png");
        SNSetEnglish(YES);
        [window setContentSize:NSMakeSize(1180,620)];
        [editor setFrameSize:NSMakeSize(1180,620)];
        [editor fit:nil];
        savePreview(editor,@"build/qa/instances-en.png");
        assert([editor adoptFile:v4]);
        [editor selectOnly:3];
        [window setContentSize:NSMakeSize(1180,840)];
        [editor setFrameSize:NSMakeSize(1180,840)];
        [editor fit:nil];
        savePreview(editor,@"build/qa/arranger-en.png");
        [window close];
        SNSetEnglish(originalEnglish);
        if(originalLanguage)[SNLanguagePreferences() setObject:originalLanguage forKey:@"SoundnetLanguage"];
        else [SNLanguagePreferences() removeObjectForKey:@"SoundnetLanguage"];
        if(ownPreferences)[SNLanguagePreferences() removePersistentDomainForName:preferenceSuite];
        [SNLanguagePreferences() synchronize];
        puts("PASS: native insertion/undo/drop/roundtrip, grouped controls, bilingual UI/data preservation, sidebar sizing, stop/resume/trigger and Chinese/English layout previews.");
    }
}
