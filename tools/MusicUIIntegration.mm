#import "../ui/TriEditor.h"
#import "../ui/WorkflowFile.hpp"
#include "../app/PatchRouting.hpp"
#include <memory>
#include <cassert>
NSView *find(NSView *view,NSString *identifier) {
    if([view.identifier isEqual:identifier])return view;
    for(NSView *child in view.subviews)if(NSView *hit=find(child,identifier))return hit;
    return nil;
}
void choose(NSView *root,int node) {
    NSPopUpButton *menu=(NSPopUpButton*)find(root,@"workflow.inspector");
    [menu selectItemWithTag:node];[menu sendAction:menu.action to:menu.target];
}
int main(int argc,char**argv){@autoreleasepool{
    [NSApplication sharedApplication];
    auto engine=std::make_unique<tri::Engine>();auto *e=engine.get();
    auto layout=tri::layout::defaults();auto *l=&layout;
    tri::RouteEdit edit;auto *transaction=&edit;
    TRIRead read=^float(int id){return tri::layout::contains(id)?(*l)[id-tri::layout::first]:e->get(id);};
    TRIWrite write=^(int id,float value){
        if(tri::layout::contains(id)){(*l)[id-tri::layout::first]=value;return;}
        if(transaction->handle(id,value,[&]{return e->routing();},[&](auto graph){e->setRouting(graph);} ))return;
        e->set(id,value);
    };
    NSView *root=TRICreateEditor(read,write,^(unsigned char s,unsigned char a,unsigned char b){e->midi(s,a,b);},
        ^BOOL{return NO;},^(BOOL){},^{return @"48 kHz · MIDI";});
    NSWindow *window=[[NSWindow alloc]initWithContentRect:root.frame styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
    window.contentView=root;[window display];
    // Activate the complete composition workflow through its real menu action.
    for(NSView *child in root.subviews)if([child isKindOfClass:NSPopUpButton.class]){
        NSPopUpButton *menu=(NSPopUpButton*)child;
        for(NSMenuItem *item in menu.itemArray)if([item.title isEqual:@"和声编曲工作流"])[NSApp sendAction:item.action to:item.target from:item];
    }
    assert((int)read(tri::layout::visibility)==16383);
    choose(root,tri::ChordNode);
    NSTextField *field=(NSTextField*)find(root,@"chord.symbols");
    assert(field);field.stringValue=@"F#m7";[field sendAction:field.action to:field.target];
    assert(e->get(tri::ChordRoot)==66&&e->get(tri::ChordQuality)==4);
    choose(root,tri::ProgressNode);
    field=(NSTextField*)find(root,@"progression.symbols");
    assert(field);field.stringValue=@"Cmaj7 | Am7 | Dm7 | G7";[field sendAction:field.action to:field.target];
    assert(e->get(tri::ProgressLength)==4&&e->get(tri::ProgressRoot2)==69);
    NSDictionary *state=ternaryfile::snapshot(read);
    NSData *data=[NSJSONSerialization dataWithJSONObject:state options:NSJSONWritingPrettyPrinted error:nil];
    NSDictionary *parsed=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    assert(ternaryfile::validate(parsed,nullptr));
    e->set(tri::ProgressRoot2,50);ternaryfile::restore(parsed,read,write);
    assert(e->get(tri::ProgressRoot2)==69);
    if(argc>1){
        [window display];[root layoutSubtreeIfNeeded];
        auto *image=[root bitmapImageRepForCachingDisplayInRect:root.bounds];
        [root cacheDisplayInRect:root.bounds toBitmapImageRep:image];
        [[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}]writeToFile:@(argv[1]) atomically:YES];
    }
    if(argc>2)[data writeToFile:@(argv[2]) atomically:YES];
    puts("PASS: composition workflow menu, 14 nodes, custom chord/progression input and full JSON roundtrip.");
}}
