#import "WorkflowAssets.h"
#import <CoreML/CoreML.h>
#include "StandardMidiFile.hpp"
#include <cmath>

namespace triassets {
namespace {
bool fail(NSString **why, NSString *message) {
    if (why) *why=message;
    return false;
}
bool named(id asset) {
    if (![asset isKindOfClass:NSDictionary.class]) return false;
    id name=asset[@"name"];
    return [name isKindOfClass:NSString.class] && [name length]>0 && [name length]<=256;
}
bool numeric(id value) {
    return [value isKindOfClass:NSNumber.class] && CFGetTypeID((__bridge CFTypeRef)value)!=CFBooleanGetTypeID();
}
bool parseClip(NSData *bytes, tri::MidiClip& clip, NSString **why) {
    try {
        clip=tri::smf::parse(static_cast<const uint8_t *>(bytes.bytes),bytes.length);
        return true;
    } catch (const std::exception& error) {
        return fail(why,[NSString stringWithUTF8String:error.what()]);
    }
}
bool parsePitches(id list, tri::NoteModel& model, NSString **why) {
    if (![list isKindOfClass:NSArray.class] || [list count]!=128)
        return fail(why,@"音高模型需要 pitches 数组，按 MIDI 输入音高 0–127 排列 128 个输出值。");
    for (int i=0; i<128; ++i) {
        id entry=list[i];
        if (!numeric(entry)) return fail(why,@"映射中的音高应为数字。");
        double v=[entry doubleValue];
        if (!std::isfinite(v) || v!=std::trunc(v) || v<0 || v>127)
            return fail(why,@"映射中的音高应为 0–127 的整数。");
        model.pitches[i]=static_cast<uint8_t>(v);
    }
    return true;
}
NSData *readLimited(NSURL *url, NSUInteger limit, NSString **why) {
    NSError *error=nil;
    NSNumber *size=nil;
    if (![url getResourceValue:&size forKey:NSURLFileSizeKey error:&error] || !size || size.unsignedLongLongValue>limit) {
        fail(why,error.localizedDescription ?: @"所选文件过大。");
        return nil;
    }
    NSData *data=[NSData dataWithContentsOfURL:url options:NSDataReadingMappedIfSafe error:&error];
    if (!data || data.length>limit) {
        fail(why,error.localizedDescription ?: @"无法读取完整文件。");
        return nil;
    }
    return data;
}
NSArray *evaluatePitchModel(NSURL *url, NSString **why) {
    NSError *error=nil;
    NSURL *temporary=nil, *compiled=url;
    if (![url.pathExtension.lowercaseString isEqualToString:@"mlmodelc"]) {
        temporary=[MLModel compileModelAtURL:url error:&error];
        if (!temporary) { fail(why,error.localizedDescription ?: @"Core ML 编译失败。"); return nil; }
        compiled=temporary;
    }
    @try {
        MLModelConfiguration *config=[MLModelConfiguration new];
        config.computeUnits=MLComputeUnitsCPUOnly;
        MLModel *model=[MLModel modelWithContentsOfURL:compiled configuration:config error:&error];
        if (!model) { fail(why,error.localizedDescription ?: @"Core ML 载入失败。"); return nil; }
        NSDictionary *inputs=model.modelDescription.inputDescriptionsByName;
        NSDictionary *outputs=model.modelDescription.outputDescriptionsByName;
        MLFeatureDescription *in=inputs.allValues.firstObject, *out=outputs.allValues.firstObject;
        auto scalar=[](MLFeatureType t) { return t==MLFeatureTypeInt64 || t==MLFeatureTypeDouble; };
        if (inputs.count!=1 || outputs.count!=1 || !scalar(in.type) || !scalar(out.type)) {
            fail(why,@"请选择一个标量音高输入、一个标量音高输出的 Core ML 模型（Int64 或 Double）。");
            return nil;
        }
        NSMutableArray *result=[NSMutableArray arrayWithCapacity:128];
        for (int pitch=0; pitch<128; ++pitch) {
            NSString *predictionError=nil;
            @autoreleasepool {
                MLFeatureValue *input=in.type==MLFeatureTypeInt64 ? [MLFeatureValue featureValueWithInt64:pitch] : [MLFeatureValue featureValueWithDouble:pitch];
                MLDictionaryFeatureProvider *features=[[MLDictionaryFeatureProvider alloc] initWithDictionary:@{in.name:input} error:&error];
                id<MLFeatureProvider> prediction=features ? [model predictionFromFeatures:features error:&error] : nil;
                MLFeatureValue *v=[prediction featureValueForName:out.name];
                if (!v || v.isUndefined) {
                    predictionError=error.localizedDescription ?: @"模型未返回音高。";
                } else {
                    double note=v.type==MLFeatureTypeInt64 ? double(v.int64Value) : v.doubleValue;
                    double rounded=std::round(note);
                    if (!std::isfinite(note) || rounded<0 || rounded>127)
                        predictionError=[NSString stringWithFormat:@"模型对输入 %d 的输出不在 MIDI 音高范围内。",pitch];
                    else
                        [result addObject:@(static_cast<int>(rounded))];
                }
            }
            if (predictionError) { fail(why,predictionError); return nil; }
        }
        return result;
    } @finally {
        if (temporary) [[NSFileManager defaultManager] removeItemAtURL:temporary error:nil];
    }
}
}

bool decode(NSDictionary *assets, tri::MidiClip& clip, tri::NoteModel& model, NSString **why) {
    if (![assets isKindOfClass:NSDictionary.class]) return fail(why,@"工作流素材应为对象。");
    for (id key in assets)
        if (!([key isEqual:@"clip"] || [key isEqual:@"model"])) return fail(why,@"工作流包含未知类型的素材。");
    tri::MidiClip nextClip;
    tri::NoteModel nextModel;
    NSDictionary *c=assets[@"clip"], *m=assets[@"model"];
    if (c) {
        if (!named(c)) return fail(why,@"Clip 缺少文件名。");
        for (id key in c) if (![@[@"name",@"smf",@"notes",@"beats"] containsObject:key]) return fail(why,@"Clip 含有未知字段。");
        NSString *encoded=c[@"smf"];
        if (![encoded isKindOfClass:NSString.class] || encoded.length>4*((tri::smf::maxFileBytes+2)/3))
            return fail(why,@"Clip 数据缺失或过大。");
        NSData *data=[[NSData alloc] initWithBase64EncodedString:encoded options:0];
        if (!data) return fail(why,@"Clip 编码无效。");
        if (!parseClip(data,nextClip,why)) return false;
        size_t count=std::count_if(nextClip.events.begin(),nextClip.events.end(),[](auto& e){return (e.status&0xf0)==0x90;});
        if ((c[@"notes"] && (!numeric(c[@"notes"]) || [c[@"notes"] doubleValue]!=count)) ||
            (c[@"beats"] && (!numeric(c[@"beats"]) || !std::isfinite([c[@"beats"] doubleValue]) || std::abs([c[@"beats"] doubleValue]-nextClip.lengthBeats)>1e-6)))
            return fail(why,@"Clip 的长度或音符数与 MIDI 内容不符。");
    }
    if (m) {
        if (!named(m)) return fail(why,@"模型缺少名称。");
        for (id key in m) if (![@[@"name",@"pitches"] containsObject:key]) return fail(why,@"音高模型含有未知字段。");
        if (!parsePitches(m[@"pitches"],nextModel,why)) return false;
    }
    clip=std::move(nextClip);
    model=nextModel;
    return true;
}
bool validate(NSDictionary *assets, NSString **why) {
    tri::MidiClip clip;
    tri::NoteModel model;
    return decode(assets,clip,model,why);
}
NSDictionary *importAsset(NSString *kind, NSURL *url, NSString **why) {
    if (!url.isFileURL || !url.lastPathComponent.length || url.lastPathComponent.length>256) {
        fail(why,@"请选择本机文件。"); return nil;
    }
    const BOOL access=[url startAccessingSecurityScopedResource];
    @try {
        NSString *name=url.lastPathComponent;
        if ([kind isEqual:@"clip"]) {
            NSData *data=readLimited(url,tri::smf::maxFileBytes,why);
            tri::MidiClip clip;
            if (!data || !parseClip(data,clip,why)) return nil;
            size_t count=std::count_if(clip.events.begin(),clip.events.end(),[](auto& e){return (e.status&0xf0)==0x90;});
            return @{@"name":name,@"smf":[data base64EncodedStringWithOptions:0],@"beats":@(clip.lengthBeats),@"notes":@(count)};
        }
        if (![kind isEqual:@"model"]) { fail(why,@"未知的素材类型。"); return nil; }
        NSArray *pitches=nil;
        NSString *suffix=url.pathExtension.lowercaseString;
        if ([suffix isEqual:@"json"]) {
            NSData *data=readLimited(url,64*1024,why);
            NSError *error=nil;
            id obj=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:&error] : nil;
            if (!data) return nil;
            if (![obj isKindOfClass:NSDictionary.class]) { fail(why,error.localizedDescription ?: @"请选择音高映射 JSON。"); return nil; }
            pitches=obj[@"pitches"];
            tri::NoteModel check;
            if (!parsePitches(pitches,check,why)) return nil;
        } else if ([@[@"mlmodel",@"mlpackage",@"mlmodelc"] containsObject:suffix]) {
            pitches=evaluatePitchModel(url,why);
            if (!pitches) return nil;
        } else { fail(why,@"请选择 Core ML 模型或音高映射 JSON。"); return nil; }
        return @{@"name":name,@"pitches":pitches};
    } @finally {
        if (access) [url stopAccessingSecurityScopedResource];
    }
}
} // namespace triassets
