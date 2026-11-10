#import "../ui/TriEditor.h"
#import "../ui/SNLocalization.h"
#import "../src/WorkflowAssets.h"
#include "../src/NodeRegistry.hpp"
#include "../platform/NodeEditor.h"
#include "../nodes/task-example/Task.hpp"
#include "../nodes/yue2/Task.hpp"
#include <cassert>
#include <cstdio>
#include <functional>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
@interface NSView (SoundnetTaskTests)
-(void)selectOnly:(uint64_t)uid;
-(void)tick;
-(void)beginTaskSession;
-(BOOL)commit:(NSDictionary *)previous;
-(void)configureTaskContext:(tri::NodeEditorContext *)context forNode:(uint64_t)uid;
@end
static NSMutableDictionary *copyDocument(NSDictionary *value) {
    NSData *bytes=[NSJSONSerialization dataWithJSONObject:value options:0 error:nil];
    return [NSJSONSerialization JSONObjectWithData:bytes options:NSJSONReadingMutableContainers error:nil];
}
static NSMutableDictionary *findNode(NSView *editor,uint64_t id) {
    for(NSMutableDictionary *node in [editor valueForKey:@"document"][@"nodes"])
        if([node[@"id"] unsignedLongLongValue]==id)return node;
    return nil;
}
static bool waitFor(NSView *editor,const std::function<bool()>& predicate,double timeout=5) {
    NSDate *until=[NSDate dateWithTimeIntervalSinceNow:timeout];
    do {
        [editor tick];
        if(predicate())return true;
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.02]];
    } while(until.timeIntervalSinceNow>0);
    return predicate();
}
// The exact submission the yue2 panel performs: an immutable request built
// from the node snapshot, run through the node's job and applied by the
// node's applier.
static bool submitGeneration(NSView *editor,uint64_t id) {
    tri::NodeEditorContext context;
    [editor configureTaskContext:&context forNode:id];
    if(!context.submitTask)return false;
    NSDictionary *node=findNode(editor,id);
    if(!node)return false;
    NSString *work=[NSTemporaryDirectory() stringByAppendingPathComponent:
        [@"soundnet-editor-test-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    tri::musicgen::GenRequest request=tri::nodes::yue2::makeRequest(node,@{},work,false);
    std::string why;
    return context.submitTask(
        tri::nodes::yue2::makeJob(std::move(request),@"YuE2"),
        tri::nodes::yue2::applyResult,why);
}
static void cancelGeneration(NSView *editor,uint64_t id) {
    tri::NodeEditorContext context;
    [editor configureTaskContext:&context forNode:id];
    if(context.cancelTask)context.cancelTask();
}
int main() { @autoreleasepool {
    setenv("SOUNDNET_NO_AUTOSAVE","1",1);
    [NSApplication sharedApplication];
    char pattern[]="/tmp/soundnet-task-editor-XXXXXX";
    NSString *directory=@(mkdtemp(pattern));
    NSString *script=[directory stringByAppendingPathComponent:@"generate.sh"];
    NSString *body=@"#!/bin/sh\ndir=\"\"\nwhile [ $# -gt 0 ]; do\nif [ \"$1\" = \"--out-dir\" ]; then dir=\"$2\"; shift 2; else shift; fi\ndone\nsleep .2\nprintf 'X:1\\nM:4/4\\nL:1/4\\nK:C\\nC D E F|\\n' > \"$dir/score.abc\"\n";
    assert([body writeToFile:script atomically:YES encoding:NSUTF8StringEncoding error:nil]);
    chmod(script.fileSystemRepresentation,0755);
    __block NSDictionary *assets=@{};
    __block BOOL acceptApply=YES;
    auto rejectedApplies=std::make_shared<std::atomic<unsigned>>(0);
    NSView *editor=TRICreateEditor(^float(int){return 0;},^(int,float){},^(uint8_t,uint8_t,uint8_t){},
        ^BOOL{return NO;},^(BOOL){},^NSString*{return @"TEST";},^NSDictionary*{return assets;},
        ^BOOL(NSDictionary *replacement){
            if(!acceptApply) {++*rejectedApplies;return NO;}
            assets=copyDocument(replacement);return YES;
        });
    NSMutableDictionary *document=copyDocument([editor valueForKey:@"document"]);
    NSMutableArray *nodes=document[@"nodes"];
    uint64_t a=100,b=101;
    auto makeNode=[&](uint64_t id) {
        return [@{@"id":@(id),@"nodeKey":@"soundnet.yue2",@"nodeVersion":@1,@"stateVersion":@1,
            @"x":@0,@"y":@0,@"parameters":@{@"gen.output":@1},
            @"texts":@{@"yue2.style":@"test",@"gen.execPath":script,@"gen.modelPath":directory}} mutableCopy];
    };
    [nodes addObject:makeNode(a)];[nodes addObject:makeNode(b)];
    [editor setValue:document forKey:@"document"];[editor commit:nil];

    // Fill a backend path after opening the inspector and submit from that
    // same panel. The initial inspector snapshot has no usable backend path.
    findNode(editor,a)[@"texts"]=@{@"yue2.style":@"test",@"gen.execPath":@"/missing/audiocpp_cli",
        @"gen.modelPath":directory};
    [editor commit:nil];
    [editor selectOnly:a];
    NSView *inspector=[editor valueForKey:@"inspector"];
    NSTextField *backendField=nil;
    NSButton *generateButton=nil;
    for(NSView *view in inspector.subviews) {
        if([view isKindOfClass:NSTextField.class]&&
            [[(NSTextField *)view stringValue] isEqual:@"/missing/audiocpp_cli"])
            backendField=(NSTextField *)view;
        for(NSView *control in view.subviews) {
            if([control isKindOfClass:NSButton.class]&&[(NSButton *)control action]==@selector(run:))
                generateButton=(NSButton *)control;
        }
    }
    assert(backendField&&generateButton);
    backendField.stringValue=script;
    assert([NSApp sendAction:backendField.action to:backendField.target from:backendField]);
    assert([NSApp sendAction:generateButton.action to:generateButton.target from:generateButton]);
    assert(waitFor(editor,[&]{return [findNode(editor,a)[@"texts"][@"yue2.score"] length]>0;}));
    assert(tri::projectresources::collection(findNode(editor,a),@"generation.takes").count==1);
    assert([assets[@"graph"][@"nodes"] count]>=2);

    // Two nodes run independently. Cancelling B must not cancel A.
    document=copyDocument([editor valueForKey:@"document"]);
    findNode(editor,a)[@"texts"]=@{@"yue2.style":@"second",@"gen.execPath":script,@"gen.modelPath":directory,
        @"yue2.score":findNode(editor,a)[@"texts"][@"yue2.score"]};
    [editor commit:document];
    [editor selectOnly:a];
    assert(submitGeneration(editor,a));
    [editor selectOnly:b];
    assert(submitGeneration(editor,b));
    cancelGeneration(editor,b);
    assert(waitFor(editor,[&]{return [findNode(editor,a)[@"texts"][@"yue2.score"] length]>0;}));
    assert(![findNode(editor,b)[@"texts"][@"yue2.score"] length]);

    // Reopening an equal project creates a new runtime session and rejects the
    // former session's result even though persisted IDs/configuration match.
    [editor selectOnly:b];
    assert(submitGeneration(editor,b));
    [editor beginTaskSession];
    NSDate *settled=[NSDate dateWithTimeIntervalSinceNow:.6];
    assert(waitFor(editor,[&]{return settled.timeIntervalSinceNow<=0;}));
    assert(![findNode(editor,b)[@"texts"][@"yue2.score"] length]);

    // Removing and recreating the same instance ID cannot resurrect a result.
    [editor selectOnly:b];
    assert(submitGeneration(editor,b));
    document=copyDocument([editor valueForKey:@"document"]);
    [[editor valueForKey:@"document"][@"nodes"] removeObject:findNode(editor,b)];[editor commit:document];
    document=copyDocument([editor valueForKey:@"document"]);
    [[editor valueForKey:@"document"][@"nodes"] addObject:makeNode(b)];[editor commit:document];
    settled=[NSDate dateWithTimeIntervalSinceNow:.6];
    assert(waitFor(editor,[&]{return settled.timeIntervalSinceNow<=0;}));
    assert(![findNode(editor,b)[@"texts"][@"yue2.score"] length]);
    [editor beginTaskSession];

    // A separately discovered non-AI module uses only the public editor task
    // capability. There is no task-example branch in the central editor.
    document=copyDocument([editor valueForKey:@"document"]);
    NSMutableDictionary *example=[@{@"id":@102,@"nodeKey":@"soundnet.task-example",
        @"nodeVersion":@1,@"stateVersion":@1,@"x":@0,@"y":@0,@"parameters":@{},
        @"texts":@{@"request":@"independent 节点"}} mutableCopy];
    [[editor valueForKey:@"document"][@"nodes"] addObject:example];[editor commit:document];
    tri::NodeEditorContext context;
    [editor configureTaskContext:&context forNode:102];
    assert(context.submitTask&&context.cancelTask&&context.taskSnapshot);
    std::string why;
    assert(context.submitTask(tri::nodes::task_example::makeTask("independent 节点"),
        [](const soundnet::TaskResult& result,NSDictionary *current)->NSDictionary* {
            if(result.status!=soundnet::TaskStatus::Succeeded)return nil;
            auto payload=std::dynamic_pointer_cast<const tri::nodes::task_example::TextResult>(result.payload);
            assert(payload);
            NSMutableDictionary *replacement=[current mutableCopy];
            NSMutableDictionary *texts=[current[@"texts"] mutableCopy];
            texts[@"result"]=[NSString stringWithUTF8String:payload->text.c_str()];
            replacement[@"texts"]=texts;return replacement;
        },why));
    assert(waitFor(editor,[&]{return [findNode(editor,102)[@"texts"][@"result"] isEqual:@"INDEPENDENT 节点"];}));
    // The old panel cannot submit captured old inputs under a new revision.
    assert(!context.submitTask(tri::nodes::task_example::makeTask("old"),
        [](const soundnet::TaskResult&,NSDictionary*)->NSDictionary* {return nil;},why));

    // A successful background calculation can still be rejected by the host.
    // Preserve both documents and keep the failed commit diagnostic visible.
    [editor selectOnly:102];
    tri::NodeEditorContext rejectedContext;
    [editor configureTaskContext:&rejectedContext forNode:102];
    NSDictionary *documentBefore=copyDocument([editor valueForKey:@"document"]);
    NSDictionary *assetsBefore=copyDocument(assets);
    bool applyReached=false;
    acceptApply=NO;
    assert(rejectedContext.submitTask([](soundnet::TaskContext&) {
        auto payload=std::make_shared<tri::nodes::task_example::TextResult>();payload->text="must roll back";
        return soundnet::TaskResult{soundnet::TaskStatus::Succeeded,payload,"TASK-SUCCESS-MARKER"};
    },[&](const soundnet::TaskResult& result,NSDictionary *current)->NSDictionary* {
        assert(result.status==soundnet::TaskStatus::Succeeded);applyReached=true;
        auto payload=std::dynamic_pointer_cast<const tri::nodes::task_example::TextResult>(result.payload);
        assert(payload);
        NSMutableDictionary *replacement=[current mutableCopy];
        NSMutableDictionary *texts=[current[@"texts"] mutableCopy];
        texts[@"result"]=[NSString stringWithUTF8String:payload->text.c_str()];
        replacement[@"texts"]=texts;return replacement;
    },why));
    assert(waitFor(editor,[&]{return rejectedApplies->load()>0;}));
    assert(applyReached);
    assert([[editor valueForKey:@"document"] isEqual:documentBefore]);
    assert([assets isEqual:assetsBefore]);
    NSTextField *status=[editor valueForKey:@"status"];
    assert(![status.stringValue containsString:@"TASK-SUCCESS-MARKER"]);
    assert([status.stringValue isEqual:SNLocalized(@"宿主无法应用节点图。")]);
    acceptApply=YES;

    // Misusing the node editor capability from a worker must not touch UI or
    // cancel an unrelated valid invocation of that same node's task.
    tri::NodeEditorContext threadContext;
    [editor configureTaskContext:&threadContext forNode:102];
    std::atomic<bool> activeStarted{false},releaseActive{false},wrongJobRan{false};
    bool activeApplied=false;
    assert(threadContext.submitTask([&](soundnet::TaskContext& task) {
        activeStarted=true;
        while(!releaseActive&&!task.cancelled())task.waitFor(std::chrono::milliseconds(10));
        return task.cancelled()?soundnet::TaskResult::cancelled():soundnet::TaskResult{};
    },[&](const soundnet::TaskResult& result,NSDictionary*)->NSDictionary* {
        assert(result.status==soundnet::TaskStatus::Succeeded);activeApplied=true;return nil;
    },why));
    assert(waitFor(editor,[&]{return activeStarted.load();}));
    std::thread wrongThread([&] { @autoreleasepool {
        std::string error;
        assert(!threadContext.submitTask([&](soundnet::TaskContext&) {
            wrongJobRan=true;return soundnet::TaskResult{};
        },[](const soundnet::TaskResult&,NSDictionary*)->NSDictionary* {return nil;},error));
        assert(!error.empty());
        threadContext.cancelTask();
        soundnet::TaskService::Snapshot unchanged;unchanged.binding.id=987654;
        assert(!threadContext.taskSnapshot(unchanged));
        assert(unchanged.binding.id==987654);
    }});
    wrongThread.join();
    soundnet::TaskService::Snapshot activeSnapshot;
    assert(threadContext.taskSnapshot(activeSnapshot));
    assert(activeSnapshot.active()&&!activeSnapshot.cancellationRequested);
    assert(!wrongJobRan);
    releaseActive=true;
    assert(waitFor(editor,[&]{return activeApplied;}));
    // History works without a chosen folder, including score-only records.
    // Publish a second complete take through the same public transaction.
    [editor selectOnly:a];
    tri::NodeEditorContext historyContext;
    [editor configureTaskContext:&historyContext forNode:a];
    auto payload=std::make_shared<tri::musicgen::GeneratedTake>();
    float audioFrames[]={.1f,-.1f,.2f,-.2f};
    payload->asset=@{@"name":@"Historical audio",@"rate":@48000,@"channels":@2,
        @"pcm":[[NSData dataWithBytes:audioFrames length:sizeof(audioFrames)] base64EncodedStringWithOptions:0]};
    payload->score=@"X:2\nQ:1/4=90\nL:1/4\nK:D\nD E|\n";
    payload->label=@"History";payload->date=@"test";
    const NSUInteger historyCount=tri::projectresources::collection(findNode(editor,a),@"generation.takes").count+1;
    assert(historyContext.submitTask([payload](soundnet::TaskContext&) {
        return soundnet::TaskResult{soundnet::TaskStatus::Succeeded,payload,{}};
    },tri::nodes::yue2::applyResult,why));
    assert(waitFor(editor,[&]{return tri::projectresources::collection(findNode(editor,a),@"generation.takes").count==historyCount;}));
    assert([findNode(editor,a)[@"asset"] isEqual:payload->asset]);
    assert([findNode(editor,a)[@"texts"][@"yue2.score"] isEqual:payload->score]);
    auto selectTake=[&](NSInteger position) {
        [editor selectOnly:a];
        NSView *inspector=[editor valueForKey:@"inspector"];
        NSPopUpButton *popup=nil;
        for(NSView *view in inspector.subviews)for(NSView *control in view.subviews)
            if([control isKindOfClass:NSPopUpButton.class]&&[(NSPopUpButton *)control action]==@selector(takeChanged:))
                popup=(NSPopUpButton *)control;
        assert(popup);[popup selectItemAtIndex:position];
        assert([NSApp sendAction:popup.action to:popup.target from:popup]);
    };
    selectTake(1); // first score-only take must clear a later audio selection
    assert(!findNode(editor,a)[@"asset"]);
    assert(![findNode(editor,a)[@"texts"][@"yue2.score"] isEqual:payload->score]);
    selectTake((NSInteger)historyCount);
    assert([findNode(editor,a)[@"asset"] isEqual:payload->asset]);
    assert([findNode(editor,a)[@"texts"][@"yue2.score"] isEqual:payload->score]);
    NSURL *project=[NSURL fileURLWithPath:[directory stringByAppendingPathComponent:@"Project/History.json"]];
    NSString *saveWhy=nil;
    assert(triassets::writeProject([editor valueForKey:@"document"],project,&saveWhy));
    NSURL *relocated=[NSURL fileURLWithPath:[directory stringByAppendingPathComponent:@"Moved"]];
    assert([NSFileManager.defaultManager moveItemAtURL:project.URLByDeletingLastPathComponent toURL:relocated error:nil]);
    NSDictionary *restored=triassets::readProject([relocated URLByAppendingPathComponent:@"History.json"],&saveWhy);
    assert(restored);
    NSDictionary *restoredNode=nil;
    for(NSDictionary *node in restored[@"nodes"])if([node[@"id"] unsignedLongLongValue]==a)restoredNode=node;
    NSArray *restoredTakes=tri::projectresources::collection(restoredNode,@"generation.takes");
    assert(restoredTakes.count==historyCount);
    assert([tri::projectresources::entryResource(restoredNode,restoredTakes.lastObject,@"audio")[@"data"] isEqual:payload->asset]);
    assert([tri::projectresources::entryResource(restoredNode,restoredTakes.lastObject,@"score")[@"text"] isEqual:payload->score]);
    // The node-owned ABC action creates the destination through the platform.
    [editor selectOnly:a];
    inspector=[editor valueForKey:@"inspector"];
    NSButton *importButton=nil;
    for(NSView *view in inspector.subviews)for(NSView *control in view.subviews)
        if([control isKindOfClass:NSButton.class]&&[(NSButton *)control action]==@selector(importScore:))importButton=(NSButton *)control;
    assert(importButton);
    NSUInteger beforeCount=[[editor valueForKey:@"document"][@"nodes"] count];
    assert([NSApp sendAction:importButton.action to:importButton.target from:importButton]);
    assert([[editor valueForKey:@"document"][@"nodes"] count]==beforeCount+1);
    NSDictionary *created=[[editor valueForKey:@"document"][@"nodes"] lastObject];
    assert([created[@"nodeKey"] isEqual:@"soundnet.midi-clip"]);
    assert([created[@"texts"][@"clip.score"] isEqual:payload->score]);
    assert([created[@"parameters"][@"clock.bpm"] doubleValue]==90);
    [[NSFileManager defaultManager] removeItemAtPath:directory error:nil];
    std::puts("PASS: editor task publication, isolation, independent module, host rejection and thread ownership");
} }
