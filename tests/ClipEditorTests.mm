#import "../ui/TriEditor.h"
#import "../src/WorkflowAssets.h"
#include "../src/StandardMidiFile.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>

@interface NSView (SoundnetClipActions)
- (void)insert:(NSMenuItem *)item;
- (void)edit:(id)sender;
- (void)editClip:(id)sender;
- (void)clipApply:(id)sender;
- (void)clipQuantize:(id)sender;
- (void)clipLaneChanged:(id)sender;
- (void)clipControllerChanged:(id)sender;
- (id)clipRoll;
- (double)laneTop;
- (NSRect)rectForNote:(NSDictionary *)note;
@end

static id openClipEditor(NSView *editor) {
    NSView *inspector=[editor valueForKey:@"inspector"];
    for(NSView *panel in inspector.subviews)if([panel isKindOfClass:NSClassFromString(@"SNClipCapturePanel")]) {
        [panel edit:nil];return [panel valueForKey:@"clipController"];
    }
    assert(false);return nil;
}

static NSEvent *mouse(NSView *view,NSEventType type,NSPoint point,NSInteger clicks=1,
                      NSEventModifierFlags modifiers=0) {
    return [NSEvent mouseEventWithType:type location:[view convertPoint:point toView:nil]
        modifierFlags:modifiers timestamp:0 windowNumber:view.window.windowNumber
        context:nil eventNumber:1 clickCount:clicks pressure:1];
}

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
    const NSUInteger initialNodeCount=[[[editor valueForKey:@"document"] objectForKey:@"nodes"] count];
    assert(initialNodeCount==2);
    NSMenuItem *item=[NSMenuItem new];item.tag=tri::nodeTypeID("soundnet.midi-clip");[editor insert:item];
    assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+1);
    NSDictionary *inserted=[assets[@"graph"][@"nodes"] lastObject];
    const uint64_t clipID=[inserted[@"id"] unsignedLongLongValue];
    assert(clipID);
    id clipEditor=openClipEditor(editor);
    id roll=[clipEditor clipRoll];
    assert(roll);
    NSView *view=roll;
    [view mouseDown:mouse(view,NSEventTypeLeftMouseDown,NSMakePoint(110,115),2)];
    [view mouseUp:mouse(view,NSEventTypeLeftMouseUp,NSMakePoint(110,115))];
    [view mouseDown:mouse(view,NSEventTypeLeftMouseDown,NSMakePoint(285,115),2)];
    [view mouseUp:mouse(view,NSEventTypeLeftMouseUp,NSMakePoint(285,115))];
    NSArray *drawn=[roll valueForKey:@"notes"];
    assert(drawn.count==2);
    const auto first=[view rectForNote:drawn[0]];
    const NSPoint pick=NSMakePoint(NSMidX(first),NSMidY(first));
    [view mouseDown:mouse(view,NSEventTypeLeftMouseDown,pick,1,NSEventModifierFlagShift)];
    [view mouseUp:mouse(view,NSEventTypeLeftMouseUp,pick)];
    assert([[roll valueForKey:@"selectedNotes"] count]==2);
    const double previousDistance=[drawn[1][@"start"] doubleValue]-[drawn[0][@"start"] doubleValue];
    const double previousStart=[drawn[0][@"start"] doubleValue];
    [view mouseDown:mouse(view,NSEventTypeLeftMouseDown,pick)];
    [view mouseDragged:mouse(view,NSEventTypeLeftMouseDragged,NSMakePoint(pick.x+60,pick.y))];
    [view mouseUp:mouse(view,NSEventTypeLeftMouseUp,NSMakePoint(pick.x+60,pick.y))];
    assert([drawn[0][@"start"] doubleValue]>previousStart);
    assert([drawn[1][@"start"] doubleValue]-[drawn[0][@"start"] doubleValue]==previousDistance);
    NSMutableDictionary *note=[@{@"pitch":@60,@"start":@0.37,@"length":@0.62,@"velocity":@100} mutableCopy];
    [roll setValue:@[note] forKey:@"notes"];
    [clipEditor clipQuantize:nil];
    [clipEditor clipApply:nil];
    NSDictionary *node=[assets[@"graph"][@"nodes"] lastObject];
    assert([node[@"id"] unsignedLongLongValue]==clipID && [node[@"nodeKey"] isEqual:@"soundnet.midi-clip"]);
    assert(node[@"asset"] && [node[@"asset"][@"notes"] intValue]==1);
    tri::GraphSpec graph;NSString *why=nil;
    assert(triassets::decodeGraph(assets[@"graph"],graph,&why));
    assert(graph.nodes.size()==initialNodeCount+1);
    const auto decoded=std::find_if(graph.nodes.begin(),graph.nodes.end(),[&](const tri::GraphNode& value){return value.id==clipID;});
    assert(decoded!=graph.nodes.end() && decoded->clip.lengthBeats==4);
    NSData *data=[[NSData alloc]initWithBase64EncodedString:node[@"asset"][@"smf"] options:0];
    assert(data.length>0);
    auto clip=tri::smf::parse(static_cast<const uint8_t *>(data.bytes),data.length);
    assert(clip.events.size()==2);
    assert(std::fabs(clip.events[0].beat-.25)<1e-6);
    assert(std::fabs(clip.events[1].beat-418.0/480)<1e-6);

    // Applying the piano roll twice exercises both serialization and import
    // back into the edit view, including simultaneous notes on separate channels.
    clipEditor=openClipEditor(editor);
    roll=[clipEditor clipRoll];
    const double tick=1.0/tri::smf::clipTicksPerBeat;
    [roll setValue:@128 forKey:@"lengthBeats"];
    [roll setValue:@[
        [@{@"pitch":@60,@"start":@1,@"length":@(tick),@"velocity":@90,
           @"channel":@2,@"offVelocity":@32} mutableCopy],
        [@{@"pitch":@60,@"start":@1,@"length":@0.125,@"velocity":@72,
           @"channel":@4,@"offVelocity":@16} mutableCopy]
    ] forKey:@"notes"];
    NSPopUpButton *lane=[clipEditor valueForKey:@"clipLanePopup"];
    [lane selectItemAtIndex:1];
    [[clipEditor valueForKey:@"clipLaneChannel"] setIntegerValue:3];
    [[clipEditor valueForKey:@"clipLaneNumber"] setIntegerValue:64];
    [clipEditor clipLaneChanged:lane];
    view=roll;
    const NSPoint point=NSMakePoint(210,[view laneTop]+20);
    [view mouseDown:mouse(view,NSEventTypeLeftMouseDown,point)];
    [view mouseUp:mouse(view,NSEventTypeLeftMouseUp,point)];
    [[clipEditor valueForKey:@"clipControllerBeat"] setDoubleValue:.75];
    [[clipEditor valueForKey:@"clipControllerValue"] setIntegerValue:127];
    [clipEditor clipControllerChanged:nil];
    assert([[roll valueForKey:@"controllers"] count]==1);
    [clipEditor clipApply:nil];
    clipEditor=openClipEditor(editor);
    [clipEditor clipApply:nil];

    node=[assets[@"graph"][@"nodes"] lastObject];
    data=[[NSData alloc]initWithBase64EncodedString:node[@"asset"][@"smf"] options:0];
    clip=tri::smf::parse(static_cast<const uint8_t *>(data.bytes),data.length,true);
    assert(clip.lengthBeats==128 && clip.events.size()==5);
    assert(clip.events[0].status==0xb2 && clip.events[0].data1==64 &&
           clip.events[0].data2==127 && clip.events[0].beat==.75);
    assert((clip.events[1].status==0x92 || clip.events[1].status==0x94) &&
           (clip.events[1].status^clip.events[2].status)==6);
    assert(clip.events[3].status==0x82 && clip.events[3].data2==32);
    assert(clip.events[4].status==0x84 && clip.events[4].data2==16);
    assert(std::fabs(clip.events[3].beat-1-tick)<1e-9);
    assert(std::fabs(clip.events[4].beat-1.125)<1e-9);
    assert(triassets::decodeGraph(assets[@"graph"],graph,&why));
    assert(graph.nodes.back().clip.events[0].status==0xb2);
    puts("PASS: piano roll quantize, controller lane and save/reopen preserve MIDI channels and clip length");
} }
