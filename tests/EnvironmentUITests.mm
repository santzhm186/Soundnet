#import "../ui/TriEditor.h"
#import "../ui/SNLocalization.h"
#import "../platform/ProjectDocument.hpp"
#import "../app/WorkflowAssetStore.hpp"
#include <cassert>
#include <memory>

@interface NSView (EnvironmentActions)
-(void)blank:(id)sender;
-(void)remove:(id)sender;
-(void)inspect;
-(void)insert:(NSMenuItem*)sender;
-(BOOL)commit:(NSDictionary*)previous;
-(void)selectAll:(id)sender;
-(void)duplicate:(id)sender;
-(void)copy:(id)sender;
-(void)paste:(id)sender;
@end
static NSDictionary* findNode(NSDictionary* document,const char* key) {
    for(NSDictionary* node in document[@"nodes"])if([node[@"nodeKey"] isEqual:@(key)])return node;
    return nil;
}
static NSPopUpButton* popupIn(NSView* view,NSString* label) {
    if([view isKindOfClass:NSPopUpButton.class]&&[view.accessibilityLabel isEqual:label])return (NSPopUpButton*)view;
    for(NSView* child in view.subviews)if(auto* popup=popupIn(child,label))return popup;
    return nil;
}
static NSDictionary* snapshot(NSDictionary* document) {
    NSData* data=[NSJSONSerialization dataWithJSONObject:document options:0 error:nil];
    return [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
}
static void assertClipEnvironment(NSDictionary* document,NSUInteger clips) {
    assert([document[@"nodes"] count]==2+clips);
    NSUInteger environments=0,timecodes=0,settings=0;
    NSMutableSet* clipIDs=[NSMutableSet new];
    for(NSDictionary* node in document[@"nodes"]) {
        environments+=tri::isEnvironmentNode(node);
        timecodes+=[node[@"nodeKey"] isEqual:@"soundnet.timecode"];
        settings+=[node[@"nodeKey"] isEqual:@"soundnet.project-settings"];
        if([node[@"nodeKey"] isEqual:@"soundnet.midi-clip"])[clipIDs addObject:node[@"id"]];
    }
    assert(environments==2&&timecodes==1&&settings==1&&clipIDs.count==clips);
    assert([document[@"edges"] count]==0);
    tri::GraphSpec graph;assert(triassets::decodeGraph(document,graph,nullptr));
}
int main(){setenv("SOUNDNET_NO_AUTOSAVE","1",1);@autoreleasepool {
    [NSApplication sharedApplication];
    auto engine=std::make_shared<tri::Engine>();
    auto store=std::make_shared<tri::WorkflowAssetStore>();
    __block BOOL managed=NO;
    NSView* editor=TRICreateEditor(^float(int p){return 0.f;},^(int,float){},^(uint8_t,uint8_t,uint8_t){},
        nil,nil,^NSString*{return @"TEST";},^NSDictionary*{return store->current;},
        ^BOOL(NSDictionary* replacement){return store->replace(replacement,*engine);},nil,nil,nil,^NSDictionary*{
            return @{@"hostManaged":@(managed),@"actualRate":@48000,@"deviceRate":@44100,
                @"devices":@[@{@"uid":@"test-in",@"name":@"Test Input",@"input":@YES,@"output":@NO},
                             @{@"uid":@"test-out",@"name":@"Test Output",@"input":@NO,@"output":@YES}]};
        });
    NSDictionary* document=[editor valueForKey:@"document"];
    assert([document[@"nodes"] count]==2);
    assert(findNode(document,"soundnet.timecode") && findNode(document,"soundnet.project-settings"));
    assert(!findNode(document,"soundnet.clock"));
    assert([document[@"settings"][@"environmentRequired"] boolValue]);
    tri::GraphSpec graph;assert(triassets::decodeGraph(document,graph,nullptr));
    assert(graph.environmentRequired);
    NSString* firstProject=document[@"projectId"];
    [editor blank:nil];document=[editor valueForKey:@"document"];
    assert(![firstProject isEqual:document[@"projectId"]] && [document[@"nodes"] count]==2);
    NSDictionary* settings=findNode(document,"soundnet.project-settings");
    [editor setValue:settings[@"id"] forKey:@"selected"];
    [editor remove:nil];assert([[[editor valueForKey:@"document"] objectForKey:@"nodes"] count]==2);
    NSMenuItem* insert=[NSMenuItem new];insert.tag=tri::nodeTypeForKey("soundnet.project-settings");
    [editor insert:insert];assert([[[editor valueForKey:@"document"] objectForKey:@"nodes"] count]==2);
    [editor inspect];
    NSString* inputLabel=SNNodeLocalized(tri::nodeTypeForKey("soundnet.project-settings"),@"输入设备");
    NSPopUpButton* input=popupIn(editor,inputLabel);
    assert(input && input.enabled && input.numberOfItems==2);
    [input selectItemAtIndex:1];[NSApp sendAction:input.action to:input.target from:input];
    settings=findNode([editor valueForKey:@"document"],"soundnet.project-settings");
    assert([settings[@"texts"][@"project.inputDevice"] isEqual:@"test-in"]);
    managed=YES;[editor inspect];
    input=popupIn(editor,inputLabel);
    assert(input && !input.enabled);
    // The authoritative loader must reject removal even outside this editor.
    NSMutableDictionary* broken=[document mutableCopy];broken[@"nodes"]=@[];
    assert(!triassets::decodeGraph(broken,graph,nullptr));
    assert(!store->replace(@{@"graph":broken},*engine));
    assert([store->current[@"graph"][@"nodes"] count]==2);
    // Keep source-project IDs distinct to verify the destination retains its own environment.
    document=[editor valueForKey:@"document"];
    NSDictionary* before=snapshot(document);
    ((NSMutableDictionary*)findNode(document,"soundnet.timecode"))[@"id"]=@101;
    ((NSMutableDictionary*)findNode(document,"soundnet.project-settings"))[@"id"]=@102;
    assert([editor commit:before]);
    insert.tag=tri::nodeTypeID("soundnet.midi-clip");[editor insert:insert];
    assertClipEnvironment([editor valueForKey:@"document"],1);
    [editor selectAll:nil];[editor duplicate:nil];
    document=[editor valueForKey:@"document"];
    assertClipEnvironment(document,2);
    assert([findNode(document,"soundnet.timecode")[@"id"] isEqual:@101]);
    assert([findNode(document,"soundnet.project-settings")[@"id"] isEqual:@102]);
    assert([[editor valueForKey:@"selection"] count]==1);
    assert([store->current[@"graph"] isEqual:document]);

    [editor selectAll:nil];[editor copy:nil];
    NSString* copiedProject=document[@"projectId"];
    [editor blank:nil];document=[editor valueForKey:@"document"];
    assertClipEnvironment(document,0);
    NSNumber* newTimecodeID=findNode(document,"soundnet.timecode")[@"id"];
    NSNumber* newSettingsID=findNode(document,"soundnet.project-settings")[@"id"];
    assert(![copiedProject isEqual:document[@"projectId"]]&&![newTimecodeID isEqual:@101]);
    NSDictionary* freshSettings=snapshot(findNode(document,"soundnet.project-settings"));
    [editor paste:nil];document=[editor valueForKey:@"document"];
    assertClipEnvironment(document,2);
    assert([findNode(document,"soundnet.timecode")[@"id"] isEqual:newTimecodeID]);
    assert([findNode(document,"soundnet.project-settings")[@"id"] isEqual:newSettingsID]);
    assert([findNode(document,"soundnet.project-settings") isEqual:freshSettings]);
    assert([[editor valueForKey:@"selection"] count]==2);
    assert([store->current[@"graph"] isEqual:document]);
    puts("PASS: environment singleton protection, device ownership, duplicate and cross-project paste");
}}
