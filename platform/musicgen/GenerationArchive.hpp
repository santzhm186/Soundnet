#pragma once
// MusicGen — take archive shared by the `soundnet.yue2` and `soundnet.ace-step`
// nodes. Runs inside a platform TaskService job on a worker thread:
//
//   * The generation result is imported as an embedded audio asset (the node
//     plays it through the graph, no external file needed).
//   * If the project declares a folder (project-settings `project.folder`),
//     the WAV is additionally copied to `<folder>/AI 生成/<节点名>-<序号>-<时间戳>.wav`
//     and every take is logged in the node's hidden `gen.takes` text as JSON.
//
// The take log is append-only; switching the active take happens in the node
// editor (it re-imports an archived file). Job captures own immutable request
// data and never touch editor objects. Objective-C++: included from editor
// and host translation units only, never from audio-thread code.
#import <Foundation/Foundation.h>
#include "MusicGenBackend.hpp"
#import "../../src/WorkflowAssets.h"

namespace tri::musicgen {

// Payload delivered to the node's result-applier on the control thread.
struct GeneratedTake final:soundnet::TaskPayload {
    NSDictionary* __strong asset=nil;    // embedded audio, nil for score-only
    NSString* __strong score=nil;        // generated ABC text, nil if none
    NSString* __strong takesJSON=nil;    // replacement for the gen.takes text
    NSString* __strong archivedPath=nil; // @"" when nothing was archived
    bool wantAudio=false;
};

namespace detail {

inline NSArray* decodeTakes(NSString* json) {
    if(![json isKindOfClass:NSString.class]||!json.length)return @[];
    NSData* data=[json dataUsingEncoding:NSUTF8StringEncoding];
    id parsed=data?[NSJSONSerialization JSONObjectWithData:data options:0 error:nil]:nil;
    return [parsed isKindOfClass:NSArray.class]?parsed:@[];
}

inline NSString* sanitizeLabel(NSString* name) {
    if(![name isKindOfClass:NSString.class]||!name.length)return @"Take";
    NSCharacterSet* forbidden=[NSCharacterSet characterSetWithCharactersInString:@"/:\\\t\n\r"];
    NSArray* parts=[name componentsSeparatedByCharactersInSet:forbidden];
    NSString* cleaned=[parts componentsJoinedByString:@"-"];
    return cleaned.length?cleaned:@"Take";
}

inline NSString* timestamp() {
    static NSDateFormatter* formatter;
    static dispatch_once_t once;
    dispatch_once(&once,^{formatter=[NSDateFormatter new];
        formatter.dateFormat=@"yyyyMMdd-HHmmss";formatter.locale=[NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];});
    return [formatter stringFromDate:[NSDate date]];
}

inline NSString* archiveSubfolder() {return @"AI 生成";}

} // namespace detail

inline soundnet::TaskService::Job generationJob(GenRequest request,NSString* nodeLabel,
                                                NSString* projectFolder,NSString* previousTakes) {
    return [request=std::move(request),nodeLabel=[nodeLabel copy],
            projectFolder=[projectFolder copy],previousTakes=[previousTakes copy]]
           (soundnet::TaskContext& context)->soundnet::TaskResult {
        @autoreleasepool {
            NSString* work=[NSString stringWithUTF8String:request.workDir.c_str()];
            @try {
                if(![[NSFileManager defaultManager] createDirectoryAtPath:work
                    withIntermediateDirectories:YES attributes:nil error:nil])
                    return soundnet::TaskResult::failure("无法创建生成工作目录。");
                AudioCppCliBackend backend;
                const auto outcome=executeGeneration(backend,request,context);
                if(outcome.status==GenTaskStatus::Cancelled)return soundnet::TaskResult::cancelled();
                if(outcome.status!=GenTaskStatus::Succeeded)return soundnet::TaskResult::failure(outcome.reason);
                if(context.cancelled())return soundnet::TaskResult::cancelled();
                auto payload=std::make_shared<GeneratedTake>();
                payload->wantAudio=!outcome.result.wavPath.empty();
                NSString* importWhy=nil;
                if(payload->wantAudio)payload->asset=triassets::importAsset(@"audio",
                    [NSURL fileURLWithPath:[NSString stringWithUTF8String:outcome.result.wavPath.c_str()]],&importWhy);
                if(payload->wantAudio&&!payload->asset)
                    return soundnet::TaskResult::failure(importWhy.UTF8String ?: "生成结果无法导入。");
                if(!outcome.result.abcPath.empty()) {
                    // The yue2 score text budget is 256 KiB. Read a bounded
                    // prefix and fail before publishing oversized text.
                    NSFileHandle* file=[NSFileHandle fileHandleForReadingAtPath:
                        [NSString stringWithUTF8String:outcome.result.abcPath.c_str()]];
                    NSData* data=[file readDataOfLength:256*1024+1];[file closeFile];
                    if(!data || data.length>256*1024)
                        return soundnet::TaskResult::failure("生成的乐谱为空或超过文本容量。");
                    payload->score=[[NSString alloc]initWithData:data encoding:NSUTF8StringEncoding];
                    if(!payload->score.length)
                        return soundnet::TaskResult::failure("生成的乐谱不是有效的 UTF-8 文本。");
                }
                // A cancellation that arrived after generation must not leave
                // an archived WAV behind: check before any archive write.
                if(context.cancelled())return soundnet::TaskResult::cancelled();
                // Archive: only when the project declared a folder. The node
                // stays self-contained without one — the take list is still
                // updated, pointing at the archived path when it exists.
                payload->archivedPath=@"";
                NSMutableArray* takes=[NSMutableArray arrayWithArray:
                    tri::musicgen::detail::decodeTakes(previousTakes)];
                NSString* folder=[projectFolder isKindOfClass:NSString.class]?projectFolder:nil;
                if(payload->wantAudio&&folder.length) {
                    NSString* parent=[folder stringByAppendingPathComponent:
                        tri::musicgen::detail::archiveSubfolder()];
                    NSError* error=nil;
                    if(![[NSFileManager defaultManager] createDirectoryAtPath:parent
                        withIntermediateDirectories:YES attributes:nil error:&error])
                        return soundnet::TaskResult::failure(
                            [[NSString stringWithFormat:@"无法创建归档文件夹：%@",error.localizedDescription]
                             UTF8String] ?: "无法创建归档文件夹。");
                    NSString* name=[NSString stringWithFormat:@"%@-%lu-%@.wav",
                        tri::musicgen::detail::sanitizeLabel(nodeLabel),
                        (unsigned long)(takes.count+1),tri::musicgen::detail::timestamp()];
                    NSString* destination=[parent stringByAppendingPathComponent:name];
                    if(![[NSFileManager defaultManager]
                        copyItemAtPath:[NSString stringWithUTF8String:outcome.result.wavPath.c_str()]
                        toPath:destination error:&error])
                        return soundnet::TaskResult::failure(
                            [[NSString stringWithFormat:@"无法归档生成文件：%@",error.localizedDescription]
                             UTF8String] ?: "无法归档生成文件。");
                    payload->archivedPath=destination;
                }
                if(payload->wantAudio||payload->score) {
                    double seconds=0,rate=0;int channels=0;
                    if(payload->asset) {
                        rate=[payload->asset[@"rate"] doubleValue];
                        channels=[payload->asset[@"channels"] intValue];
                        NSData* pcm=[[NSData alloc]initWithBase64EncodedString:payload->asset[@"pcm"] ?: @"" options:0];
                        seconds=rate>0?double(pcm.length)/4.0/double(std::max(1,channels))/rate:0;
                    }
                    [takes addObject:@{
                        @"name":[NSString stringWithFormat:@"%@-%lu",
                            tri::musicgen::detail::sanitizeLabel(nodeLabel),(unsigned long)takes.count+1],
                        @"file":payload->archivedPath ?: @"",
                        @"seconds":@(seconds),@"rate":@(rate),@"channels":@(channels),
                        @"date":tri::musicgen::detail::timestamp(),
                        @"hasScore":@(payload->score!=nil)}];
                    payload->takesJSON=[[NSString alloc]initWithData:
                        [NSJSONSerialization dataWithJSONObject:takes options:0 error:nil]
                        encoding:NSUTF8StringEncoding] ?: @"";
                }
                return {soundnet::TaskStatus::Succeeded,payload,{}};
            } @catch(NSException* exception) {
                return soundnet::TaskResult::failure(exception.reason.UTF8String ?: "生成结果无法读取。");
            }
        }
    };
}

} // namespace tri::musicgen
