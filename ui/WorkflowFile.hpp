#pragma once
#import "TriEditor.h"
#import "../src/WorkflowAssets.h"
#include "../src/Engine.hpp"
#include "../app/PatchLayout.hpp"
#include <cmath>

namespace ternaryfile {
inline NSArray<NSString*> *names() {
    return @[@"", @"midi", @"arp", @"osc1", @"osc2", @"osc3", @"input", @"mix", @"eq", @"delay", @"output", @"chord", @"progression", @"clock", @"velocity", @"dawMidi", @"clip", @"model", @"midiOut"];
}
inline NSArray<NSString*> *ports() {
    return @[@"arp.notes", @"osc1.notes", @"osc2.notes", @"osc3.notes",
             @"mix.1", @"mix.2", @"mix.3", @"mix.4", @"eq.audio", @"delay.audio", @"output.audio", @"chord.notes", @"velocity.notes", @"arp.clock", @"progression.clock", @"velocity.clock", @"clip.clock", @"model.notes", @"midiOut.notes"];
}
inline bool number(id object, double lo, double hi, bool integer = false) {
    if (![object isKindOfClass:NSNumber.class] || CFGetTypeID((__bridge CFTypeRef)object) == CFBooleanGetTypeID()) return false;
    const double v = [object doubleValue];
    return std::isfinite(v) && v >= lo && v <= hi && (!integer || v == std::round(v));
}
inline NSDictionary *snapshot(TRIRead read, TRIAssetsRead assetsRead = nullptr) {
    NSDictionary *assets=assetsRead ? assetsRead() : @{};
    if (!assets) return nil;
    NSMutableDictionary *parameters = [NSMutableDictionary new], *connections = [NSMutableDictionary new], *layout = [NSMutableDictionary new];
    for (int id=0; id<tri::ParamCount; ++id) if(!tri::routeParam(id)) parameters[@(tri::params[id].name)] = @(read(id));
    for (int i=0; i<(int)ports().count; ++i) {
        int n = (int)std::lround(read(tri::routeID(i)));
        if (n > 0 && n < (int)names().count) connections[ports()[i]] = names()[n];
    }
    int visible = (int)std::lround(read(tri::layout::visibility));
    for (int n=1; n<=tri::layout::nodeCount; ++n)
        if (visible & (1<<(n-1))) layout[names()[n]] = @[@(read(tri::layout::x(n))), @(read(tri::layout::y(n)))];
    return @{@"format":@"audio.ternary.workflow", @"version":@2, @"assets":assets, @"parameters":parameters, @"connections":connections, @"nodes":layout};
}
// Validate the whole file before writing any part to the live instrument.
inline bool validate(id object, NSString **why) {
    auto fail = [&](NSString *reason) { if (why) *why = reason; return false; };
    if (![object isKindOfClass:NSDictionary.class]) return fail(@"文件内容应为工作流对象。");
    NSDictionary *file = object;
    if (![file[@"format"] isEqual:@"audio.ternary.workflow"] || !number(file[@"version"],1,2,true))
        return fail(@"请选择 Soundnet 导出的工作流 JSON。");
    NSDictionary *params = file[@"parameters"], *nodes = file[@"nodes"], *links = file[@"connections"];
    if (![params isKindOfClass:NSDictionary.class] || ![nodes isKindOfClass:NSDictionary.class] || ![links isKindOfClass:NSDictionary.class])
        return fail(@"工作流缺少参数、节点或接线。");
    if (!triassets::validate(file[@"assets"] ?: @{},why)) return false;
    for (int i=0;i<tri::ParamCount;++i) {
        if(tri::routeParam(i))continue;
        const auto &spec = tri::params[i];
        id val = params[@(spec.name)];
        if(i>=tri::ChordRoot && !val)continue;
        const bool discrete = i==tri::Pattern || i==tri::ArpRepeat || i==tri::ArpOn || i==tri::ClipRun || i==tri::ClipLoop || i==tri::MidiLocalOnly ||
            i==tri::Wave1 || i==tri::Wave2 || i==tri::Wave3 || i==tri::Tune1 || i==tri::Tune2 || i==tri::Tune3;
        // JSON writes float values as short decimals. Compare bounds after conversion
        // to the same Float32 representation used by the DSP.
        if (!number(val,-100000,100000,discrete) || [val floatValue]<spec.min || [val floatValue]>spec.max || (i>=tri::Osc1Attack && i<=tri::Osc3Release && [val floatValue]<0 && [val floatValue]!=tri::Follow)) return fail([NSString stringWithFormat:@"参数 %@ 超出范围或缺失。",@(spec.name)]);
    }
    for (id key in nodes) {
        NSUInteger n=[names() indexOfObject:key]; NSArray *pos=nodes[key];
        if (n==NSNotFound || n==0 || ![pos isKindOfClass:NSArray.class] || pos.count!=2 ||
            !number(pos[0],-1,65535) || !number(pos[1],-1,65535) ||
            !tri::layout::valid(tri::layout::x((int)n),[pos[0] floatValue]) || !tri::layout::valid(tri::layout::y((int)n),[pos[1] floatValue])) return fail(@"节点名称或坐标无效。");
    }
    tri::Routing routing{};
    for (id key in links) {
        NSUInteger i=[ports() indexOfObject:key], source=[names() indexOfObject:links[key]];
        if (i==NSNotFound || source==NSNotFound || source==0) return fail(@"存在无法识别的端口。");
        NSString *dest = [key componentsSeparatedByString:@"."].firstObject;
        if (!nodes[links[key]] || !nodes[dest]) return fail(@"连线指向了已移除的节点。");
        routing[i]=(int)source;
    }
    if (!tri::validRouting(routing)) return fail(@"接线包含回路，或端口的信号类型不匹配。");
    return true;
}
inline bool restore(NSDictionary *file, TRIRead read, TRIWrite write, TRIAssetsRead assetsRead = nullptr, TRIAssetsWrite assetsWrite = nullptr) {
    NSDictionary *incoming=file[@"assets"] ?: @{};
    NSDictionary *current=assetsRead ? assetsRead() : @{};
    if (!current) return false;
    if (![incoming isEqual:current] && (!assetsWrite || !assetsWrite(incoming))) return false;
    NSDictionary *params=file[@"parameters"], *nodes=file[@"nodes"], *links=file[@"connections"];
    auto change = [&](int id, float v){ if (read(id)!=v) write(id,v); };
    int visible=0;
    for (int n=1; n<=tri::layout::nodeCount; ++n) {
        NSArray *pos=nodes[names()[n]];
        if (pos) visible |= 1<<(n-1);
        change(tri::layout::x(n), pos ? [pos[0] floatValue] : -1);
        change(tri::layout::y(n), pos ? [pos[1] floatValue] : -1);
    }
    change(tri::layout::visibility,visible);
    write(tri::layout::beginRouting,0);
    for (int i=0;i<(int)ports().count;++i) {
        NSString *node=links[ports()[i]];
        write(tri::routeID(i),node ? (float)[names() indexOfObject:node] : 0);
    }
    write(tri::layout::commitRouting,0);
    for (int i=0;i<tri::ParamCount;++i) if(!tri::routeParam(i))change(i,params[@(tri::params[i].name)]?[params[@(tri::params[i].name)] floatValue]:tri::params[i].def);
    return true;
}
}
