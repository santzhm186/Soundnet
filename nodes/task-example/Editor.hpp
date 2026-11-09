#pragma once
#include "../../platform/NodeEditor.h"
#include "Task.hpp"
#import <objc/runtime.h>
#ifdef SNInstanceEditor
#define SNTaskExampleAction SNAUTaskExampleAction
#endif
namespace tri::nodes::task_example {
inline NSDictionary* applyResult(const soundnet::TaskResult& result,NSDictionary* current) {
    if(result.status!=soundnet::TaskStatus::Succeeded)return nil;
    auto payload=std::dynamic_pointer_cast<const TextResult>(result.payload);
    if(!payload)return nil;
    NSMutableDictionary* replacement=[current mutableCopy];
    NSMutableDictionary* texts=[current[@"texts"] isKindOfClass:NSDictionary.class]
        ? [current[@"texts"] mutableCopy] : [NSMutableDictionary new];
    texts[@"result"]=[NSString stringWithUTF8String:payload->text.c_str()];
    replacement[@"texts"]=texts;return replacement;
}
}
@interface SNTaskExampleAction : NSObject {
@public
    tri::NodeEditorContext context;
}
@property(nonatomic,strong) NSTextField* status;
-(void)run:(id)sender;
-(void)cancel:(id)sender;
@end
@implementation SNTaskExampleAction
-(void)run:(id)sender {
    if(!context.submitTask)return;
    NSString* input=context.configuration[@"texts"][@"request"] ?: @"hello";
    std::string why;
    if(context.submitTask(tri::nodes::task_example::makeTask(input.UTF8String ?: ""),
                          tri::nodes::task_example::applyResult,why))
        _status.stringValue=context.text(@"正在处理…");
    else _status.stringValue=[NSString stringWithUTF8String:why.c_str()];
}
-(void)cancel:(id)sender {if(context.cancelTask)context.cancelTask();}
@end
namespace tri::nodes::task_example {
inline NSView* editor(NodeEditorContext context) {
    NSView* panel=[[NSView alloc]initWithFrame:NSMakeRect(0,0,270,65)];
    SNTaskExampleAction* action=[SNTaskExampleAction new];action->context=context;
    static const char owner=0;
    objc_setAssociatedObject(panel,&owner,action,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    NSButton* run=[NSButton buttonWithTitle:context.text(@"开始") target:action action:@selector(run:)];
    run.frame=NSMakeRect(0,30,129,28);run.enabled=bool(context.submitTask);[panel addSubview:run];
    NSButton* stop=[NSButton buttonWithTitle:context.text(@"停止") target:action action:@selector(cancel:)];
    stop.frame=NSMakeRect(141,30,129,28);stop.enabled=bool(context.cancelTask);[panel addSubview:stop];
    action.status=[NSTextField labelWithString:context.text(@"空闲")];
    action.status.frame=NSMakeRect(0,2,270,22);[panel addSubview:action.status];
    return panel;
}
}
