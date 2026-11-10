#pragma once
// Generation jobs return immutable, self-contained resources. The platform's
// project writer owns materialization and references; nodes never choose an
// archive folder. Only the control-thread result applier appends history.
#import <Foundation/Foundation.h>
#include "MusicGenBackend.hpp"
#include "../ProjectResources.h"

namespace tri::musicgen {
struct GeneratedTake final:soundnet::TaskPayload {
    NSDictionary* __strong asset=nil;
    NSString* __strong score=nil;
    NSString* __strong label=nil;
    NSString* __strong date=nil;
};
inline soundnet::TaskService::Job generationJob(GenRequest request,NSString* nodeLabel) {
    NSString *work=[NSTemporaryDirectory() stringByAppendingPathComponent:
        [@"soundnet-generation-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    request.workDir=work.UTF8String ?: "";
    return [request=std::move(request),nodeLabel=[nodeLabel copy],work=[work copy]]
           (soundnet::TaskContext& context)->soundnet::TaskResult {
        @autoreleasepool {
            @try {
                if(![NSFileManager.defaultManager createDirectoryAtPath:work
                    withIntermediateDirectories:YES attributes:nil error:nil])
                    return soundnet::TaskResult::failure("无法创建生成工作目录。");
                AudioCppCliBackend backend;
                const auto outcome=executeGeneration(backend,request,context);
                if(outcome.status==GenTaskStatus::Cancelled)return soundnet::TaskResult::cancelled();
                if(outcome.status!=GenTaskStatus::Succeeded)return soundnet::TaskResult::failure(outcome.reason);
                if(context.cancelled())return soundnet::TaskResult::cancelled();
                auto payload=std::make_shared<GeneratedTake>();
                payload->label=nodeLabel ?: @"Take";
                payload->date=[[NSISO8601DateFormatter new] stringFromDate:[NSDate date]];
                if(!outcome.result.wavPath.empty()) {
                    NSString *why=nil;
                    payload->asset=tri::projectresources::importAsset(@"audio",
                        [NSURL fileURLWithPath:@(outcome.result.wavPath.c_str())],&why);
                    if(!payload->asset)return soundnet::TaskResult::failure(why.UTF8String ?: "生成结果无法导入。");
                }
                if(!outcome.result.abcPath.empty()) {
                    NSFileHandle* file=[NSFileHandle fileHandleForReadingAtPath:@(outcome.result.abcPath.c_str())];
                    NSData* data=[file readDataOfLength:256*1024+1];[file closeFile];
                    if(!data||data.length>256*1024)
                        return soundnet::TaskResult::failure("生成的乐谱为空或超过文本容量。");
                    payload->score=[[NSString alloc]initWithData:data encoding:NSUTF8StringEncoding];
                    if(!payload->score.length)return soundnet::TaskResult::failure("生成的乐谱不是有效的 UTF-8 文本。");
                }
                if(context.cancelled())return soundnet::TaskResult::cancelled();
                return {soundnet::TaskStatus::Succeeded,payload,{}};
            } @catch(NSException* exception) {
                return soundnet::TaskResult::failure(exception.reason.UTF8String ?: "生成结果无法读取。");
            } @finally {
                [NSFileManager.defaultManager removeItemAtPath:work error:nil];
            }
        }
    };
}
// Called only after TaskService has accepted the owning session/input version.
inline NSDictionary *appendTake(NSDictionary *node,const GeneratedTake& take) {
    NSMutableDictionary *resources=[NSMutableDictionary new];
    if(take.asset)resources[@"audio"]=tri::projectresources::assetResource(@"audio",take.asset);
    if(take.score)resources[@"score"]=tri::projectresources::textResource(@"Score",@"abc",take.score);
    const NSUInteger number=tri::projectresources::collection(node,@"generation.takes").count+1;
    double rate=[take.asset[@"rate"] doubleValue];
    const int channels=[take.asset[@"channels"] intValue];
    NSData *pcm=[[NSData alloc]initWithBase64EncodedString:take.asset[@"pcm"] ?: @"" options:0];
    const double seconds=rate>0&&channels>0?double(pcm.length)/(sizeof(float)*channels*rate):0;
    return tri::projectresources::appendEntry(node,@"generation.takes",
        [NSString stringWithFormat:@"%@-%lu",take.label ?: @"Take",(unsigned long)number],resources,
        @{@"date":take.date ?: @"",@"seconds":@(seconds),@"rate":@(rate),@"channels":@(channels)});
}
}
