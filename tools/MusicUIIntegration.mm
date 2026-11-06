#import "../ui/TriEditor.h"
#import "../ui/WorkflowFile.hpp"
#include "../app/PatchRouting.hpp"
#include "../app/WorkflowAssetStore.hpp"
#include <memory>
#include <cassert>
@interface NSView (WorkflowValidation)
-(void)loadAssetURL:(NSURL *)url kind:(NSString *)kind;
-(void)undoWorkflow:(id)sender;
-(void)redoWorkflow:(id)sender;
-(void)finishEdit;
-(BOOL)restore:(NSDictionary *)snapshot;
@end
static void templateNamed(NSView *editor, NSString *title) {
    for(NSView *child in editor.subviews) if([child isKindOfClass:NSPopUpButton.class]) {
        NSMenuItem *item=[[(NSPopUpButton*)child menu] itemWithTitle:title];
        if(item) { assert([NSApp sendAction:item.action to:item.target from:item]); return; }
    }
    assert(false && "missing template");
}
static void finishImport(NSView *editor, TRIAssetsRead read, NSString *kind) {
    auto deadline=[NSDate dateWithTimeIntervalSinceNow:30];
    while(!read()[kind] && [deadline timeIntervalSinceNow]>0)
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.02]];
    assert(read()[kind]);
    [editor finishEdit];
}
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
    tri::WorkflowAssetStore store;
    auto *assets=&store;
    TRIAssetsRead assetsRead=^NSDictionary *{return assets->current;};
    TRIAssetsWrite assetsWrite=^BOOL(NSDictionary *next){return assets->replace(next,*e);};
    NSView *root=TRICreateEditor(read,write,^(unsigned char s,unsigned char a,unsigned char b){e->midi(s,a,b);},
        ^BOOL{return NO;},^(BOOL){},^{return @"48 kHz · MIDI";},assetsRead,assetsWrite);
    NSWindow *window=[[NSWindow alloc]initWithContentRect:root.frame styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
    window.contentView=root;[window display];
    // Activate the complete composition workflow through its real menu action.
    templateNamed(root,@"和声编曲工作流");
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
    if(argc>2)[data writeToFile:@(argv[2]) atomically:YES];

    templateNamed(root,@"空白工作流");
    assert((int)read(tri::layout::visibility)==0);
    templateNamed(root,@"DAW MIDI 演奏");
    assert(read(tri::RouteArpIn)==tri::DawMidiNode && read(tri::MidiLocalOnly)==1);
    templateNamed(root,@"MIDI Clip → 模型 → MIDI 输出");
    assert(read(tri::RouteMidiOutIn)==tri::ModelNode && read(tri::RouteOutputIn)==0);
    auto midiOnly=ternaryfile::snapshot(read,assetsRead);
    assert([midiOnly[@"connections"][@"midiOut.notes"] isEqual:@"model"] && !midiOnly[@"nodes"][@"osc1"]);
    assert(ternaryfile::validate(midiOnly,nullptr));
    templateNamed(root,@"MIDI Clip → 模型 → 音频");
    assert(read(tri::RouteModelIn)==tri::ClipNode && read(tri::RouteClipClock)==tri::ClockNode);
    assert(read(tri::RouteOsc1In)==tri::ModelNode && read(tri::RouteOutputIn)==tri::Osc1Node);
    choose(root,tri::ClipNode);
    NSButton *import=(NSButton *)find(root,@"clip.import");
    assert(import && import.enabled && import.action==@selector(importAsset:));
    [root finishEdit];
    NSURL *folder=[NSURL fileURLWithPath:@"examples" isDirectory:YES];
    [root loadAssetURL:[folder URLByAppendingPathComponent:@"FourBeat.mid"] kind:@"clip"];
    finishImport(root,assetsRead,@"clip");
    assert([assetsRead()[@"clip"][@"notes"] intValue]==5);
    assert([[(NSTextField*)find(root,@"asset.name") stringValue] containsString:@"FourBeat.mid"]);
    choose(root,tri::ModelNode);
    assert([(NSButton*)find(root,@"model.import") isEnabled]);
    [root loadAssetURL:[folder URLByAppendingPathComponent:@"PitchMirror.mlmodel"] kind:@"model"];
    finishImport(root,assetsRead,@"model");
    assert([assetsRead()[@"model"][@"pitches"][60] intValue]==67);
    [root undoWorkflow:nil]; assert(assetsRead()[@"clip"] && !assetsRead()[@"model"]);
    [root redoWorkflow:nil]; assert(assetsRead()[@"model"] && assetsRead()[@"clip"]);
    choose(root,tri::ClipNode);
    NSButton *play=(NSButton *)find(root,[NSString stringWithFormat:@"param.%d.check",tri::ClipRun]);
    assert(play.enabled);
    play.state=NSControlStateValueOn; [play sendAction:play.action to:play.target];
    assert(read(tri::ClipRun)==1);
    std::vector<float> left(8192),right(left.size());
    e->render(nullptr,nullptr,left.data(),right.data(),(int)left.size());
    assert(*std::max_element(left.begin(),left.end())>.01f);
    play.state=NSControlStateValueOff; [play sendAction:play.action to:play.target];
    auto snapshot=ternaryfile::snapshot(read,assetsRead);
    auto json=[NSJSONSerialization dataWithJSONObject:snapshot options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:nil];
    NSDictionary *restored=[NSJSONSerialization JSONObjectWithData:json options:0 error:nil];
    assert(ternaryfile::validate(restored,nullptr));
    if(argc>1){
        [window display];[root layoutSubtreeIfNeeded];
        auto *image=[root bitmapImageRepForCachingDisplayInRect:root.bounds];
        [root cacheDisplayInRect:root.bounds toBitmapImageRep:image];
        assert([[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}]writeToFile:@(argv[1]) atomically:YES]);
    }
    assert(assetsWrite(@{}));
    templateNamed(root,@"空白工作流");
    assert([root restore:restored]);
    assert(read(tri::RouteModelIn)==tri::ClipNode && [assetsRead() isEqual:snapshot[@"assets"]]);
    choose(root,tri::ClipNode);
    if(argc>3) assert([json writeToFile:@(argv[3]) atomically:YES]);
    puts("PASS: composition input and JSON, blank canvas, DAW input template, MIDI Clip + Core ML import, playback, asset undo/redo and portable workflow roundtrip.");
}}
