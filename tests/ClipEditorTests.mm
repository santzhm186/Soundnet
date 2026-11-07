#import "../ui/TriEditor.h"
#import "../src/WorkflowAssets.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

@interface NSView (SoundnetClipActions)
- (void)insert:(NSMenuItem *)item;
- (void)editClip:(id)sender;
- (void)clipApply:(id)sender;
- (id)clipRoll;
@end

int main() { setenv("SOUNDNET_NO_AUTOSAVE","1",1); @autoreleasepool {
    [NSApplication sharedApplication];
    __block NSDictionary *assets=@{};
    NSWindow *window=[[NSWindow alloc]initWithContentRect:NSMakeRect(0,0,1144,650)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable
        backing:NSBackingStoreBuffered defer:NO];
    NSView *editor=TRICreateEditor(
        ^float(int p){return 0;},^(int p,float v){},^(uint8_t s,uint8_t a,uint8_t b){},
        ^BOOL{return NO;},^(BOOL){},^NSString *{return @"CLIP";},
        ^NSDictionary *{return assets;},
        ^BOOL(NSDictionary *replacement){
            tri::GraphSpec graph;
            if(!triassets::decodeGraph(replacement[@"graph"],graph,nullptr))return NO;
            assets=replacement;return YES;});
    window.contentView=editor;
    [window makeKeyAndOrderFront:nil];
    NSMenuItem *item=[NSMenuItem new];item.tag=tri::ClipNode;[editor insert:item];
    assert([assets[@"graph"][@"nodes"] count]==1);
    [editor editClip:nil];
    id roll=[editor clipRoll];
    assert(roll);
    NSMutableDictionary *note=[@{@"pitch":@60,@"start":@0.5,@"length":@1.0,@"velocity":@100} mutableCopy];
    [roll setValue:@[note] forKey:@"notes"];
    [editor clipApply:nil];
    NSDictionary *node=assets[@"graph"][@"nodes"][0];
    assert(node[@"asset"] && [node[@"asset"][@"notes"] intValue]==1);
    tri::GraphSpec graph;NSString *why=nil;
    assert(triassets::decodeGraph(assets[@"graph"],graph,&why));
    assert(graph.nodes.size()==1 && graph.nodes[0].clip.lengthBeats==4);
    puts("PASS: piano roll encodes a MIDI clip asset");
} }
