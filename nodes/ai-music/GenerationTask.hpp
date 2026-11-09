#pragma once
// macOS resource-import adapter for the AI node. The reusable CLI backend and
// platform TaskService remain independent of AppKit and editor instances.
#import <Foundation/Foundation.h>
#include "Backend.hpp"
#import "../../src/WorkflowAssets.h"
namespace tri::nodes::ai_music {
struct GeneratedMusic final:soundnet::TaskPayload {
    NSDictionary *__strong asset=nil;
    NSString *__strong score=nil;
    bool wantAudio=false,hadScore=false;
};
inline soundnet::TaskService::Job generationJob(GenRequest request) {
    return [request=std::move(request)](soundnet::TaskContext& context)->soundnet::TaskResult {
        @autoreleasepool {
            NSString *folder=[NSString stringWithUTF8String:request.workDir.c_str()];
            @try {
                if(![[NSFileManager defaultManager] createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil])
                    return soundnet::TaskResult::failure("无法创建生成工作目录。");
                AudioCppCliBackend backend;
                const auto outcome=executeGeneration(backend,request,context);
                if(outcome.status==GenTaskStatus::Cancelled)return soundnet::TaskResult::cancelled();
                if(outcome.status!=GenTaskStatus::Succeeded)return soundnet::TaskResult::failure(outcome.reason);
                if(context.cancelled())return soundnet::TaskResult::cancelled();
                auto payload=std::make_shared<GeneratedMusic>();
                payload->wantAudio=!outcome.result.wavPath.empty();payload->hadScore=!outcome.result.abcPath.empty();
                NSString *importWhy=nil;
                if(payload->wantAudio)payload->asset=triassets::importAsset(@"audio",
                    [NSURL fileURLWithPath:[NSString stringWithUTF8String:outcome.result.wavPath.c_str()]],&importWhy);
                if(payload->wantAudio&&!payload->asset)
                    return soundnet::TaskResult::failure(importWhy.UTF8String ?: "生成结果无法导入。");
                if(payload->hadScore) {
                    // The descriptor's default text budget is 256 KiB. Read a
                    // bounded prefix and fail before publishing oversized text.
                    NSFileHandle *file=[NSFileHandle fileHandleForReadingAtPath:
                        [NSString stringWithUTF8String:outcome.result.abcPath.c_str()]];
                    NSData *data=[file readDataOfLength:256*1024+1];[file closeFile];
                    if(!data || data.length>256*1024)
                        return soundnet::TaskResult::failure("生成的乐谱为空或超过文本容量。");
                    payload->score=[[NSString alloc]initWithData:data encoding:NSUTF8StringEncoding];
                    if(!payload->score.length)
                        return soundnet::TaskResult::failure("生成的乐谱不是有效的 UTF-8 文本。");
                }
                if(context.cancelled())return soundnet::TaskResult::cancelled();
                return {soundnet::TaskStatus::Succeeded,payload,{}};
            } @catch(NSException *exception) {
                return soundnet::TaskResult::failure(exception.reason.UTF8String ?: "生成结果无法读取。");
            } @finally {[[NSFileManager defaultManager] removeItemAtPath:folder error:nil];}
        }
    };
}
} // namespace tri::nodes::ai_music
