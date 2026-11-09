#import "../ui/TriEditor.h"
#import "../ui/SNLocalization.h"
#import "../src/WorkflowAssets.h"
#include "../src/NodeRegistry.hpp"
#include "../platform/NodeEditor.h"
#include "../nodes/task-example/Task.hpp"
#include <cassert>
#include <cstdio>
#include <functional>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
@interface NSView (SoundnetTaskTests)
-(void)selectOnly:(uint64_t)uid;
-(void)generateAudio:(id)sender;
-(void)aiStop:(id)sender;
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
        return [@{@"id":@(id),@"nodeKey":@"soundnet.ai-music",@"nodeVersion":@1,@"stateVersion":@1,
            @"x":@0,@"y":@0,@"parameters":@{@"ai.outMode":@2},
            @"texts":@{@"ai.prompt":@"test",@"ai.execPath":script,@"ai.modelPath":directory}} mutableCopy];
    };
    [nodes addObject:makeNode(a)];[nodes addObject:makeNode(b)];
    [editor setValue:document forKey:@"document"];[editor commit:nil];

    // A configured full path succeeds and unchanged input accepts the result.
    [editor selectOnly:a];[editor generateAudio:nil];
    assert(waitFor(editor,[&]{return [findNode(editor,a)[@"texts"][@"ai.score"] length]>0;}));
    assert([assets[@"graph"][@"nodes"] count]>=2);

    // Two nodes run independently. Stopping B must not cancel A.
    document=copyDocument([editor valueForKey:@"document"]);
    findNode(editor,a)[@"texts"]=@{@"ai.prompt":@"second",@"ai.execPath":script,@"ai.modelPath":directory};
    [editor commit:document];
    [editor selectOnly:a];[editor generateAudio:nil];
    [editor selectOnly:b];[editor generateAudio:nil];[editor aiStop:nil];
    assert(waitFor(editor,[&]{return [findNode(editor,a)[@"texts"][@"ai.score"] length]>0;}));
    assert(![findNode(editor,b)[@"texts"][@"ai.score"] length]);

    // Reopening an equal project creates a new runtime session and rejects the
    // former session's result even though persisted IDs/configuration match.
    [editor selectOnly:b];
    assert(waitFor(editor,[&]{return ![[editor valueForKey:@"aiBusy"] boolValue];}));
    [editor generateAudio:nil];[editor beginTaskSession];
    NSDate *settled=[NSDate dateWithTimeIntervalSinceNow:.6];
    assert(waitFor(editor,[&]{return settled.timeIntervalSinceNow<=0;}));
    assert(![findNode(editor,b)[@"texts"][@"ai.score"] length]);

    // Removing and recreating the same instance ID cannot resurrect a result.
    [editor selectOnly:b];[editor generateAudio:nil];
    document=copyDocument([editor valueForKey:@"document"]);
    [[editor valueForKey:@"document"][@"nodes"] removeObject:findNode(editor,b)];[editor commit:document];
    document=copyDocument([editor valueForKey:@"document"]);
    [[editor valueForKey:@"document"][@"nodes"] addObject:makeNode(b)];[editor commit:document];
    settled=[NSDate dateWithTimeIntervalSinceNow:.6];
    assert(waitFor(editor,[&]{return settled.timeIntervalSinceNow<=0;}));
    assert(![findNode(editor,b)[@"texts"][@"ai.score"] length]);
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
    [[NSFileManager defaultManager] removeItemAtPath:directory error:nil];
    std::puts("PASS: editor task publication, isolation, independent module, host rejection and thread ownership");
} }
