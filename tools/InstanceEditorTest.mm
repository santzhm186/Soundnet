#import "../ui/TriEditor.h"
#import "../src/WorkflowAssets.h"
#include "../src/Engine.hpp"
#include "../src/WavFile.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <functional>
#include <memory>
#include <cassert>

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
- (void)sliderChanged:(NSSlider *)slider;
- (void)choiceChanged:(NSPopUpButton *)popup;
- (void)nameChanged:(NSTextField *)field;
- (void)bypassChanged:(NSButton *)button;
@end

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
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
            });
        for (int i=0; i<25; ++i) {
            NSMenuItem *item = [NSMenuItem new];
            item.tag = tri::Osc1Node;
            [editor insert:item];
        }
        assert([assets[@"graph"][@"nodes"] count]==25);
        NSMenuItem *start = [NSMenuItem new];
        start.tag = tri::StartNode;
        [editor insert:start];
        assert([assets[@"graph"][@"nodes"] count]==26);
        [editor undo:nil];
        assert([assets[@"graph"][@"nodes"] count]==25);
        [editor redo:nil];
        assert([assets[@"graph"][@"nodes"] count]==26);
        [editor selectAll:nil];
        assert([[editor activeIDs] count]==26);
        [editor copy:nil];
        [editor paste:nil];
        assert([assets[@"graph"][@"nodes"] count]==52);
        [editor remove:nil];
        assert([assets[@"graph"][@"nodes"] count]==26);
        [editor undo:nil];
        assert([assets[@"graph"][@"nodes"] count]==52);
        [editor redo:nil];
        assert([assets[@"graph"][@"nodes"] count]==26);
        NSMenuItem *sampler = [NSMenuItem new];
        sampler.tag = tri::SamplerNode;
        [editor insert:sampler];
        assert([assets[@"graph"][@"nodes"] count]==27);
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
        assert([assets[@"graph"][@"nodes"] count]==28);
        NSDictionary *samplerDrop = [assets[@"graph"][@"nodes"] lastObject];
        assert(samplerDrop && [samplerDrop[@"nodeKey"] isEqual:samplerKey] && samplerDrop[@"asset"][@"name"]);
        assert([editor dropFileURLs:@[dropURL] atPoint:NSMakePoint(140, 140) clipMode:YES]);
        assert([assets[@"graph"][@"nodes"] count]==29);
        NSDictionary *clipDrop = [assets[@"graph"][@"nodes"] lastObject];
        NSString *clipKey=[NSString stringWithUTF8String:tri::nodeKey(tri::AudioClipNode)];
        assert(clipDrop && [clipDrop[@"nodeKey"] isEqual:clipKey] && clipDrop[@"asset"][@"name"]);
        std::remove(dropPath.c_str());

        // Opening a v4 project must go through the canonicaliser, not the v1/v2
        // migrator.
        NSDictionary *v4=[NSJSONSerialization JSONObjectWithData:
            [NSData dataWithContentsOfFile:@"examples/three-voice-arrangement.v4.json"]
            options:NSJSONReadingMutableContainers error:nil];
        assert([editor adoptFile:v4]);
        NSDictionary *opened=[editor valueForKey:@"document"];
        assert([opened[@"version"] isEqual:@4]);
        assert([opened[@"nodes"] count]==9);
        assert([opened[@"nodes"][2][@"nodeKey"] isEqual:@"soundnet.voice-arranger"]);
        assert([opened[@"edges"][2][@"sourcePort"] isEqual:@"high"]);

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
        [editor setValue:@1 forKey:@"selected"];
        [editor commit:nil];
        [editor fit:nil];
        [editor inspect];
        [window display];
        NSBitmapImageRep *image = [editor bitmapImageRepForCachingDisplayInRect:editor.bounds];
        [editor cacheDisplayInRect:editor.bounds toBitmapImageRep:image];
        [[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}]
            writeToFile:@"build/qa/instances.png" atomically:YES];
        [window close];
        puts("PASS: 25 native oscillator instances, sampler insertion, drag-drop audio, independent trigger, undo/redo, JSON roundtrip and trigger example.");
    }
}
