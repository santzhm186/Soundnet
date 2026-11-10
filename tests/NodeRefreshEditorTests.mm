#import "../ui/TriEditor.h"
#import "../platform/NodeEditor.h"
#import "../src/WorkflowAssets.h"
#include <cassert>
#include <atomic>
#include <thread>

@interface NSView (NodeRefreshActions)
-(void)insert:(NSMenuItem*)item;
-(tri::NodeEditorContext)editorContextForNode:(uint64_t)uid;
-(void)drainBackgroundTasks;
-(void)tick;
-(void)runExport:(NSInteger)kind toURL:(NSURL*)url seconds:(double)seconds rate:(double)rate bits:(int)bits;
@end

int main(){setenv("SOUNDNET_NO_AUTOSAVE","1",1);@autoreleasepool {
    [NSApplication sharedApplication];
    const auto original=tri::nodeCatalogSnapshot();
    __block NSDictionary *assets=@{};__block BOOL playing=YES;__block int midi=0,writes=0;
    NSView *editor=TRICreateEditor(^float(int){return 0;},^(int,float){},^(uint8_t,uint8_t,uint8_t){++midi;},
        nil,nil,^NSString*{return @"CATALOG";},^NSDictionary*{return assets;},^BOOL(NSDictionary *replacement){
            tri::GraphSpec graph;if(!triassets::decodeGraph(replacement[@"graph"],graph,nullptr,false,true))return NO;
            assets=replacement;++writes;return YES;
        },^BOOL{return playing;},^(BOOL enabled){playing=enabled;});
    NSMenuItem *item=[NSMenuItem new];item.tag=tri::nodeTypeID("soundnet.gain");[editor insert:item];
    const uint64_t uid=[[editor valueForKey:@"selected"] unsignedLongLongValue];
    const auto old=[editor editorContextForNode:uid];assert(old.isCurrent&&old.isCurrent());
    auto changed=original->modules;
    for(auto& module:changed)if(std::strcmp(module.spec.key,"soundnet.gain")==0)++module.spec.nodeVersion;
    tri::publishNodeCatalog(tri::makeNodeCatalog(std::move(changed)));
    NSString *why=nil;assert(TRIRefreshNodeCatalog(editor,&why));assert(!playing);assert(!old.isCurrent());
    const int afterRefresh=writes;
    old.commit(@{@"parameters":@{@"gain":@3}});old.sendMidi(0x90,60,100);
    assert(!old.createNode(@"soundnet.gain",@{}));assert(writes==afterRefresh&&midi==0);
    tri::GraphSpec graph;assert(triassets::decodeGraph(assets[@"graph"],graph,&why,false,true));
    const auto missing=std::find_if(graph.nodes.begin(),graph.nodes.end(),[&](const auto& node){return node.id==uid;});
    assert(missing!=graph.nodes.end()&&missing->unavailable);
    auto unavailableContext=[editor editorContextForNode:uid];assert(!unavailableContext.commit&&!unavailableContext.isCurrent);
    // Restoring a supported catalog resolves the saved placeholder without losing its ID.
    auto restoredModules=original->modules;
    for(auto& module:restoredModules)if(std::strcmp(module.spec.key,"soundnet.gain")==0)
        module.resourceDirectory="/tmp/soundnet-catalog-resource-snapshot";
    tri::publishNodeCatalog(tri::makeNodeCatalog(std::move(restoredModules)));assert(TRIRefreshNodeCatalog(editor,&why));
    assert([([editor editorContextForNode:uid].resourceDirectory) isEqual:@"/tmp/soundnet-catalog-resource-snapshot"]);
    assert(triassets::decodeGraph(assets[@"graph"],graph,&why,false,true));
    const auto restored=std::find_if(graph.nodes.begin(),graph.nodes.end(),[&](const auto& node){return node.id==uid;});
    assert(restored!=graph.nodes.end()&&!restored->unavailable);
    // An old background task can neither publish nor resubmit after refresh.
    item.tag=tri::nodeTypeID("soundnet.task-example");[editor insert:item];
    const uint64_t taskID=[[editor valueForKey:@"selected"] unsignedLongLongValue];
    auto task=[editor editorContextForNode:taskID];assert(task.submitTask);
    auto started=std::make_shared<std::atomic<bool>>(false),cancelled=std::make_shared<std::atomic<bool>>(false);
    std::string error;
    assert(task.submitTask([started,cancelled](soundnet::TaskContext& cancellation){
        started->store(true);
        while(!cancellation.cancelled())std::this_thread::sleep_for(std::chrono::milliseconds(1));
        cancelled->store(true);return soundnet::TaskResult{};
    },[](const soundnet::TaskResult&,NSDictionary*)->NSDictionary*{assert(false);return nil;},error));
    for(int i=0;i<100&&!started->load();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(started->load());
    tri::publishNodeCatalog(tri::makeNodeCatalog(original->modules));assert(TRIRefreshNodeCatalog(editor,&why));
    for(int i=0;i<100&&!cancelled->load();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(cancelled->load());[editor drainBackgroundTasks];
    assert(!task.submitTask([](soundnet::TaskContext&){return soundnet::TaskResult{};},
        [](const soundnet::TaskResult&,NSDictionary*)->NSDictionary*{return @{};},error));
    // Catalogs are shared by AU instances. Refreshing one window must also
    // cancel work and rebuild the other window before its timer drains results.
    __block NSDictionary *otherAssets=@{};__block BOOL otherPlaying=YES;__block int otherWrites=0;
    NSView *other=TRICreateEditor(^float(int){return 0;},^(int,float){},nil,nil,nil,nil,
        ^NSDictionary*{return otherAssets;},^BOOL(NSDictionary* replacement){otherAssets=replacement;++otherWrites;return YES;},
        ^BOOL{return otherPlaying;},^(BOOL enabled){otherPlaying=enabled;});
    [other insert:item];
    auto otherTask=[other editorContextForNode:[[other valueForKey:@"selected"] unsignedLongLongValue]];
    started->store(false);cancelled->store(false);
    assert(otherTask.submitTask([started,cancelled](soundnet::TaskContext& cancellation){
        started->store(true);
        while(!cancellation.cancelled())std::this_thread::sleep_for(std::chrono::milliseconds(1));
        cancelled->store(true);return soundnet::TaskResult{};
    },[](const soundnet::TaskResult&,NSDictionary*)->NSDictionary*{assert(false);return nil;},error));
    for(int i=0;i<100&&!started->load();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(started->load());const int previousWrites=otherWrites;
    tri::publishNodeCatalog(tri::makeNodeCatalog(original->modules));assert(TRIRefreshNodeCatalog(editor,&why));
    assert(!otherTask.isCurrent());[other tick];
    for(int i=0;i<100&&!cancelled->load();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(cancelled->load()&&!otherPlaying&&otherWrites>previousWrites);
    assert([[other valueForKey:@"catalogGeneration"] unsignedLongLongValue]==tri::nodeCatalogSnapshot()->generation);
    // The direct drain path is also a synchronization boundary, even when the
    // worker has already produced a result before the timer observes refresh.
    otherTask=[other editorContextForNode:[[other valueForKey:@"selected"] unsignedLongLongValue]];
    started->store(false);
    assert(otherTask.submitTask([started](soundnet::TaskContext&){started->store(true);return soundnet::TaskResult{};},
        [](const soundnet::TaskResult&,NSDictionary*)->NSDictionary*{assert(false);return nil;},error));
    for(int i=0;i<100&&!started->load();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(started->load());otherPlaying=YES;
    tri::publishNodeCatalog(tri::makeNodeCatalog(original->modules));assert(TRIRefreshNodeCatalog(editor,&why));
    [other drainBackgroundTasks];assert(!otherPlaying);
    assert([[other valueForKey:@"catalogGeneration"] unsignedLongLongValue]==tri::nodeCatalogSnapshot()->generation);
    // Even if export completion beats the other editor's timer, its old
    // catalog cannot replace a destination file after another window refreshes.
    NSURL *destination=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:
        [@"Soundnet-Refresh-" stringByAppendingString:[NSUUID.UUID.UUIDString stringByAppendingString:@".wav"]]]];
    NSData *sentinel=[@"preserve existing export" dataUsingEncoding:NSUTF8StringEncoding];
    assert([sentinel writeToURL:destination atomically:YES]);
    [other runExport:1 toURL:destination seconds:.01 rate:48000 bits:16];
    tri::publishNodeCatalog(tri::makeNodeCatalog(original->modules));assert(TRIRefreshNodeCatalog(editor,&why));
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:10];
    while([[other valueForKey:@"exportBusy"] boolValue]&&deadline.timeIntervalSinceNow>0)
        [[NSRunLoop currentRunLoop]runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    assert(![[other valueForKey:@"exportBusy"] boolValue]);
    assert([[NSData dataWithContentsOfURL:destination] isEqual:sentinel]);
    [[NSFileManager defaultManager]removeItemAtURL:destination error:nil];
    puts("PASS: shared catalog refresh cancels old UI/tasks, rebuilds both editors and preserves resource snapshots/placeholders");
}}
