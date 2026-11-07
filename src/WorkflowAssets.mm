#import "WorkflowAssets.h"
#import <CoreML/CoreML.h>
#import <AVFoundation/AVFoundation.h>
#include "StandardMidiFile.hpp"
#include "AudioFile.hpp"
#include <climits>
#include <cmath>
#include <cstring>
#include <memory>

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
bool parseAudioAsset(NSDictionary *asset,std::shared_ptr<tri::AudioSample>& out,NSString **why) {
    if (!named(asset)) return fail(why,@"采样器缺少音频名称。");
    for (id key in asset)
        if (![@[@"name",@"rate",@"channels",@"pcm"] containsObject:key])
            return fail(why,@"音频素材含有未知字段。");
    id rate=asset[@"rate"],channels=asset[@"channels"],encoded=asset[@"pcm"];
    if (!numeric(rate) || !std::isfinite([rate doubleValue]) ||
        [rate doubleValue]<1000 || [rate doubleValue]>384000)
        return fail(why,@"音频采样率无效。");
    if (!numeric(channels) || [channels doubleValue]!=std::trunc([channels doubleValue]) ||
        ([channels intValue]!=1 && [channels intValue]!=2))
        return fail(why,@"音频声道数无效。");
    const size_t maxBytes=tri::audiofile::maxSamples*sizeof(float);
    if (![encoded isKindOfClass:NSString.class] || [encoded length]>4*((maxBytes+2)/3)+4)
        return fail(why,@"音频数据缺失或过大。");
    NSData *pcm=[[NSData alloc] initWithBase64EncodedString:encoded options:0];
    if (!pcm || !pcm.length || pcm.length%sizeof(float) || pcm.length>maxBytes)
        return fail(why,@"音频数据编码无效。");
    const int channelCount=[channels intValue];
    const size_t sampleCount=pcm.length/sizeof(float);
    if (sampleCount%size_t(channelCount))
        return fail(why,@"音频采样数与声道数不符。");
    auto sample=std::make_shared<tri::AudioSample>();
    sample->name=[asset[@"name"] UTF8String] ?: "";
    sample->rate=[rate doubleValue];
    sample->channels=channelCount;
    sample->samples.resize(sampleCount);
    std::memcpy(sample->samples.data(),pcm.bytes,sample->samples.size()*sizeof(float));
    for (float value : sample->samples)
        if (!std::isfinite(value)) return fail(why,@"音频包含无效采样值。");
    if (!sample->valid()) return fail(why,@"音频参数无效。");
    out=std::move(sample);
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
        if (!([key isEqual:@"clip"] || [key isEqual:@"model"] || [key isEqual:@"graph"])) return fail(why,@"工作流包含未知类型的素材。");
    if(assets[@"graph"]) { tri::GraphSpec graph; if(!decodeGraph(assets[@"graph"],graph,why))return false; }
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

bool decodeGraph(NSDictionary *document, tri::GraphSpec& graph, NSString **why, bool decodeAssets) {
    if(![document isKindOfClass:NSDictionary.class] || ![document[@"version"] isEqual:@3] ||
       ![document[@"nodes"] isKindOfClass:NSArray.class] || ![document[@"edges"] isKindOfClass:NSArray.class])
        return fail(why,@"请选择版本 3 的 Soundnet 工作流。");
    auto integer=[](id v,double min,double max) {
        return numeric(v) && std::isfinite([v doubleValue]) && [v doubleValue]>=min &&
            [v doubleValue]<=max && std::trunc([v doubleValue])==[v doubleValue];
    };
    tri::GraphSpec next;
    if([document[@"nodes"] count]>tri::maxGraphNodes) { fail(why,@"节点数量超过上限。"); return false; }
    if([document[@"edges"] count]>tri::maxGraphEdges) { fail(why,@"连线数量超过上限。"); return false; }
    for(id obj in document[@"nodes"]) {
        if(![obj isKindOfClass:NSDictionary.class])return fail(why,@"节点格式无效。");
        if(!integer(obj[@"id"],1,9007199254740991.) || !integer(obj[@"type"],1,tri::CommentNode) ||
           !numeric(obj[@"x"]) || !numeric(obj[@"y"]) || !std::isfinite([obj[@"x"] doubleValue]) ||
           !std::isfinite([obj[@"y"] doubleValue]) || ![obj[@"parameters"] isKindOfClass:NSDictionary.class])
            return fail(why,@"节点的实例 ID、类型或坐标无效。");
        tri::GraphNode node;
        node.id=[obj[@"id"] unsignedLongLongValue];node.type=[obj[@"type"] intValue];
        node.x=[obj[@"x"] floatValue];node.y=[obj[@"y"] floatValue];
        if(obj[@"bypass"]) {
            if(![obj[@"bypass"] isKindOfClass:NSNumber.class])return fail(why,@"节点旁通值无效。");
            node.bypass=[obj[@"bypass"] boolValue];
        }
        for(id key in obj[@"parameters"]) {
            if(![key isKindOfClass:NSString.class])return fail(why,@"参数标识无效。");
            NSScanner *scan=[NSScanner scannerWithString:key];int pid=0;
            id value=obj[@"parameters"][key];
            if(![scan scanInt:&pid] || !scan.isAtEnd || !tri::validGraphParam(node.type,pid) ||
                !numeric(value))return fail(why,@"参数标识或值无效。");
            if(tri::arrangerParam(pid)) {
                const auto& spec=tri::graphParamInfo(pid);
                if(!integer(value,spec.min,spec.max))return fail(why,@"声部音域和织体须使用有效范围内的整数。");
            }
            node.parameters[pid]=[value floatValue];
        }
        if(node.type==tri::ArrangerNode && !tri::validArrangerRanges(node))
            return fail(why,@"同一声部的最低音不能高于最高音。");
        if(obj[@"asset"]) {
            NSString *kind=node.type==tri::ClipNode?@"clip":node.type==tri::ModelNode?@"model":
                node.type==tri::SamplerNode?@"audio":node.type==tri::AudioClipNode?@"audio":nil;
            if(!kind)return fail(why,@"该节点类型不能包含素材。");
            if(decodeAssets) {
                if(node.type==tri::SamplerNode || node.type==tri::AudioClipNode) {
                    std::shared_ptr<tri::AudioSample> audio;
                    if(!parseAudioAsset(obj[@"asset"],audio,why))return false;
                    node.sample=std::move(audio);
                } else if(!decode(@{kind:obj[@"asset"]},node.clip,node.model,why)) {
                    return fail(why,@"节点素材无效。");
                }
            }
        }
        next.nodes.push_back(std::move(node));
    }
    for(id edge in document[@"edges"]) {
        if(![edge isKindOfClass:NSDictionary.class] || !integer(edge[@"source"],1,9007199254740991.) ||
            !integer(edge[@"target"],1,9007199254740991.) || !integer(edge[@"port"],0,3))return fail(why,@"连线格式无效。");
        if(edge[@"sourcePort"] && !integer(edge[@"sourcePort"],0,INT_MAX))return fail(why,@"输出端口无效。");
        next.edges.push_back({[edge[@"source"] unsignedLongLongValue],[edge[@"target"] unsignedLongLongValue],[edge[@"port"] intValue],[edge[@"sourcePort"] intValue]});
    }
    std::vector<size_t> order;std::string reason;
    if(!tri::graphOrder(next,order,&reason))
        return fail(why,reason.empty()?@"工作流图无效。":[NSString stringWithUTF8String:reason.c_str()]);
    graph=std::move(next);return true;
}

bool validate(NSDictionary *assets, NSString **why) {
    tri::MidiClip clip;
    tri::NoteModel model;
    return decode(assets,clip,model,why);
}

bool decodeCompressedAudio(NSURL *url, tri::AudioSample& sample, NSString **why) {
    NSError *error=nil;
    AVAudioFile *file=[[AVAudioFile alloc] initForReading:url
        commonFormat:AVAudioPCMFormatFloat32 interleaved:NO error:&error];
    if(!file) { fail(why,error.localizedDescription ?: @"无法解码音频文件。"); return false; }
    AVAudioFormat *format=file.processingFormat;
    const double rate=format.sampleRate;
    const int channels=std::min(2,int(format.channelCount));
    if(!(rate>=1000 && rate<=384000) || channels<1) {
        fail(why,@"音频采样率需在 1–384 kHz 之间。");return false;
    }
    if(file.length<=0) {fail(why,@"音频文件没有采样。");return false;}
    const AVAudioFrameCount chunk=4096;
    AVAudioPCMBuffer *buffer=[[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:chunk];
    if(!buffer) {fail(why,@"无法分配音频解码缓冲区。");return false;}
    std::vector<float> samples;
    while(true) {
        NSError *readError=nil;
        @autoreleasepool {
            BOOL ok=[file readIntoBuffer:buffer error:&readError];
            if(!ok) {
                if(readError) {fail(why,readError.localizedDescription);return false;}
                break;
            }
            const AVAudioFrameCount frames=buffer.frameLength;
            if(!frames) break;
            if(samples.size()/size_t(channels)+size_t(frames)>tri::audiofile::maxSamples) {
                fail(why,@"音频过长；导入上限约为 4M 个采样点。");return false;
            }
            float *const *data=buffer.floatChannelData;
            if(!data || !data[0]) {fail(why,@"音频解码结果为空。");return false;}
            for(AVAudioFrameCount i=0;i<frames;++i) {
                const float left=data[0][i];
                const float right=channels>1 && data[1] ? data[1][i] : left;
                if(!std::isfinite(left) || !std::isfinite(right)) {
                    fail(why,@"音频包含无效的采样值。");return false;
                }
                samples.push_back(left);samples.push_back(right);
            }
        }
    }
    if(samples.empty()) {fail(why,@"音频文件没有采样。");return false;}
    sample.name=url.lastPathComponent.UTF8String ?: "";
    sample.rate=rate;sample.channels=channels;sample.samples=std::move(samples);
    return sample.valid();
}


NSDictionary *encodeClipAsset(NSString *name, const tri::MidiClip& clip, NSString **why) {
    try {
        std::vector<uint8_t> bytes=tri::smf::encodeClip(clip);
        NSData *data=[NSData dataWithBytes:bytes.data() length:bytes.size()];
        size_t notes=std::count_if(clip.events.begin(),clip.events.end(),[](const tri::ClipEvent& e){
            return (e.status&0xf0)==0x90 && e.data2>0;
        });
        return @{@"name":name ?: @"Clip",@"smf":[data base64EncodedStringWithOptions:0],
                 @"beats":@(clip.lengthBeats),@"notes":@(notes)};
    } catch(const std::exception& error) {
        fail(why,[NSString stringWithUTF8String:error.what()]);
        return nil;
    }
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
        if ([kind isEqual:@"audio"]) {
            NSData *data=readLimited(url,tri::audiofile::maxFileBytes,why);
            if (!data) return nil;
            tri::AudioSample sample;
            NSString *suffix=url.pathExtension.lowercaseString;
            if ([@[@"wav",@"aif",@"aiff",@"aifc"] containsObject:suffix]) {
                std::string error;
                if (!tri::audiofile::parse(static_cast<const uint8_t *>(data.bytes),data.length,sample,error)) {
                    fail(why,[NSString stringWithUTF8String:error.c_str()]);
                    return nil;
                }
            } else if (!decodeCompressedAudio(url,sample,why)) return nil;
            sample.name=[name UTF8String] ?: "";
            NSData *pcm=[NSData dataWithBytes:sample.samples.data()
                                        length:sample.samples.size()*sizeof(float)];
            return @{@"name":name,@"rate":@(sample.rate),@"channels":@(sample.channels),
                     @"pcm":[pcm base64EncodedStringWithOptions:0]};
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
