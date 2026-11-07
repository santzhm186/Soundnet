#import "../ui/TriEditor.h"
#import "../src/WorkflowAssets.h"
#include "../src/Engine.hpp"
#include "../src/WavFile.hpp"
#include <cstdio>
#include <cmath>
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
        NSDictionary *samplerNode = [[assets[@"graph"][@"nodes"] filteredArrayUsingPredicate:
            [NSPredicate predicateWithFormat:@"type == %d", tri::SamplerNode]] firstObject];
        assert(samplerNode && [samplerNode[@"parameters"] count]==7);
        std::vector<float> wave(4800), waveR(4800);
        for (size_t i=0; i<wave.size(); ++i) wave[i]=waveR[i]=0.2f*std::sin(2*M_PI*440*i/48000.0);
        std::string why;
        const std::string dropPath = "/tmp/soundnet-drop-test.wav";
        assert(tri::wav::write(dropPath, wave, waveR, 48000, why));
        NSURL *dropURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:dropPath.c_str()]];
        assert([editor dropFileURLs:@[dropURL] atPoint:NSMakePoint(100, 100) clipMode:NO]);
        assert([assets[@"graph"][@"nodes"] count]==28);
        NSDictionary *samplerDrop = [assets[@"graph"][@"nodes"] lastObject];
        assert(samplerDrop && [samplerDrop[@"type"] intValue]==tri::SamplerNode && samplerDrop[@"asset"][@"name"]);
        assert([editor dropFileURLs:@[dropURL] atPoint:NSMakePoint(140, 140) clipMode:YES]);
        assert([assets[@"graph"][@"nodes"] count]==29);
        NSDictionary *clipDrop = [assets[@"graph"][@"nodes"] lastObject];
        assert(clipDrop && [clipDrop[@"type"] intValue]==tri::AudioClipNode && clipDrop[@"asset"][@"name"]);
        std::remove(dropPath.c_str());

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
        [editor setValue:example forKey:@"document"];
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
