#pragma once
#import "WorkflowAssets.h"
#include "Engine.hpp"

namespace triassets {
inline NSDictionary *midiCaptureCommand(tri::Engine& engine,NSDictionary *command) {
    if(![command isKindOfClass:NSDictionary.class])return @{};
    NSString *action=command[@"action"];
    if([action isEqual:@"start"]) {
        if(![command[@"node"] isKindOfClass:NSNumber.class]||
           ![command[@"tempoParameter"] isKindOfClass:NSNumber.class])return @{};
        const auto token=engine.startMidiCapture([command[@"node"] unsignedLongLongValue],
                                                 [command[@"tempoParameter"] intValue]);
        return token?@{@"token":@(token)}:@{@"error":@"无法录制。请连接 MIDI 输入，并完成其他录制。"};
    }
    if(![action isEqual:@"finish"]&&![action isEqual:@"cancel"])return @{};
    if(![command[@"token"] isKindOfClass:NSNumber.class])return @{};
    auto recording=engine.finishMidiCapture([command[@"token"] unsignedLongLongValue]);
    if([action isEqual:@"cancel"])return @{};
    if(!recording)return @{@"error":@"节点或接线已变化，录制未保存。"};
    if(recording->overflowed())return @{@"error":@"录制超出容量，原片段已保留。"};
    auto clip=recording->clip();
    if(clip.events.empty())return @{@"error":@"没有收到可保存的 MIDI 事件。"};
    NSString *why=nil;
    NSDictionary *asset=encodeClipAsset(@"MIDI Recording",clip,&why);
    return asset?@{@"asset":asset}:@{@"error":why ?: @"无法保存 MIDI 录制。"};
}
} // namespace triassets
