#import "../ui/TriEditor.h"
#import "../src/WorkflowAssets.h"
#include "../src/Engine.hpp"
#include <memory>
#include <cassert>

@interface NSView (SoundnetTestActions)
- (void)insert:(NSMenuItem *)item;
- (void)undo:(id)sender;
- (void)redo:(id)sender;
- (void)commit:(NSDictionary *)previous;
- (void)fit:(id)sender;
- (void)inspect;
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
        puts("PASS: 25 native oscillator instances, independent trigger, undo/redo, JSON roundtrip and trigger example.");
    }
}
