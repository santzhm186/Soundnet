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

namespace {
// Resolve a document node to a registered kernel type. v4 identifies a node by
// `nodeKey`; v3 identified it by a numeric `type`. Unknown keys are tolerated for
// the editor (so a newer project still opens) but never for export.
bool documentNodeType(NSDictionary *obj,bool allowUnknown,int& type,bool& unavailable,NSString **why) {
    id key=obj[@"nodeKey"],legacy=obj[@"type"];
    if([key isKindOfClass:NSString.class] && [key length] && [key length]<=160) {
        const int resolved=tri::nodeTypeForKey([key UTF8String]);
        if(resolved) {
            const tri::NodeSpec *spec=tri::nodeSpec(resolved);
            if(spec && !spec->instantiable)return fail(why,@"该节点类型不能出现在工程中。");
            type=resolved;return true;
        }
        if(allowUnknown) {type=0;unavailable=true;return true;}
        return fail(why,[NSString stringWithFormat:@"未知的节点类型：%@",key]);
    }
    if(numeric(legacy)) {
        const double value=[legacy doubleValue];
        if(value!=std::trunc(value)||value<1||value>1.0e9)return fail(why,@"节点类型编号无效。");
        const tri::NodeSpec *spec=tri::nodeSpec(int(value));
        if(!spec||!spec->instantiable)return fail(why,@"节点类型编号无效或不可实例化。");
        type=int(value);return true;
    }
    return fail(why,@"节点缺少 nodeKey。");
}
// A v4 parameter key is the stable alias; a v3 key is the numeric ID. Both are
// accepted so migration never depends on field order.
int documentParamID(NSString *key,int type,bool strict,NSString **why) {
    const int byKey=tri::paramIdForKey(key.UTF8String);
    if(byKey>=0)return byKey;
    NSScanner *scan=[NSScanner scannerWithString:key];
    int pid=0;
    if([scan scanInt:&pid] && scan.isAtEnd) {
        if(!strict && tri::nodeAcceptsParam(type,pid))return pid;
        if(strict && tri::paramSpec(pid))return pid;
    }
    if(strict) {
        fail(why,[NSString stringWithFormat:@"未知的参数名称：%@",key]);
        return -1;
    }
    return -1;
}
}

bool decodeGraph(NSDictionary *document, tri::GraphSpec& graph, NSString **why, bool decodeAssets,
                 bool allowUnknownNodes, std::vector<tri::Diagnostic>* diagnostics) {
    if(![document isKindOfClass:NSDictionary.class] || ![document[@"nodes"] isKindOfClass:NSArray.class] ||
       ![document[@"edges"] isKindOfClass:NSArray.class])
        return fail(why,@"请选择 Soundnet 工作流文件。");
    const int version=[document[@"version"] intValue];
    if(!numeric(document[@"version"]) || (version!=3 && version!=4))
        return fail(why,@"请选择版本 3 或 4 的 Soundnet 工作流。");
    const bool strict=version>=4;
    auto integer=[](id v,double min,double max) {
        return numeric(v) && std::isfinite([v doubleValue]) && [v doubleValue]>=min &&
            [v doubleValue]<=max && std::trunc([v doubleValue])==[v doubleValue];
    };
    tri::GraphSpec next;
    std::vector<tri::Diagnostic> early;
    if([document[@"projectId"] isKindOfClass:NSString.class])
        next.projectId=[document[@"projectId"] UTF8String] ?: "";
    if([document[@"nodes"] count]>tri::maxGraphNodes)return fail(why,@"节点数量超过上限。");
    if([document[@"edges"] count]>tri::maxGraphEdges)return fail(why,@"连线数量超过上限。");
    for(id obj in document[@"nodes"]) {
        if(![obj isKindOfClass:NSDictionary.class])return fail(why,@"节点格式无效。");
        if(!integer(obj[@"id"],1,9007199254740991.) || !numeric(obj[@"x"]) || !numeric(obj[@"y"]) ||
           !std::isfinite([obj[@"x"] doubleValue]) || !std::isfinite([obj[@"y"] doubleValue]))
            return fail(why,@"节点的实例 ID 或坐标无效。");
        id parameters=obj[@"parameters"] ?: @{};
        if(![parameters isKindOfClass:NSDictionary.class])return fail(why,@"节点参数应为对象。");
        tri::GraphNode node;
        node.id=[obj[@"id"] unsignedLongLongValue];
        node.x=[obj[@"x"] floatValue];node.y=[obj[@"y"] floatValue];
        bool unavailable=false;
        if(!documentNodeType(obj,allowUnknownNodes,node.type,unavailable,why))return false;
        node.unavailable=unavailable;
        if(unavailable) {
            node.key=[obj[@"nodeKey"] UTF8String] ?: "";
            node.unavailableReason="缺失节点类型："+node.key;
            next.nodes.push_back(std::move(node));
            continue;
        }
        const tri::NodeSpec *spec=tri::nodeSpec(node.type);
        node.key=spec->key;
        if(numeric(obj[@"nodeVersion"]))node.nodeVersion=uint16_t([obj[@"nodeVersion"] intValue]);
        if(node.nodeVersion==0)node.nodeVersion=spec->nodeVersion;
        if(node.nodeVersion>spec->nodeVersion) {
            // Behaviour written by a newer build: the parameters may mean
            // something else. Editing can keep it, export cannot pretend.
            const std::string message=std::string("节点 ")+spec->key+" 的行为版本 "+
                std::to_string(node.nodeVersion)+" 高于当前支持的 "+std::to_string(spec->nodeVersion);
            // `allowUnknownNodes` is the editor's forward-compatibility switch;
            // strict callers (export, host publication) must not guess.
            if(!allowUnknownNodes)return fail(why,[NSString stringWithUTF8String:message.c_str()]);
            early.push_back({node.id,message,false});
        }
        if(obj[@"bypass"]) {
            if(![obj[@"bypass"] isKindOfClass:NSNumber.class])return fail(why,@"节点旁通值无效。");
            node.bypass=[obj[@"bypass"] boolValue];
            if(node.bypass && !tri::nodeHas(spec->capabilities,tri::CapBypassable))
                return fail(why,[NSString stringWithFormat:@"节点不支持旁通：%s",spec->title]);
        }
        for(id rawKey in parameters) {
            if(![rawKey isKindOfClass:NSString.class])return fail(why,@"参数标识无效。");
            id value=parameters[rawKey];
            const int pid=documentParamID(rawKey,node.type,strict,why);
            if(pid<0) {
                if(strict)return false;
                continue;   // v3 carried parameters that the node never declared.
            }
            const tri::ParamSpec *param=tri::nodeParamSpec(node.type,pid);
            if(!param) {
                if(strict)return fail(why,[NSString stringWithFormat:@"参数不属于该节点：%@",rawKey]);
                continue;
            }
            if(!numeric(value))return fail(why,@"参数值应为数字。");
            const double number=[value doubleValue];
            const tri::ParamInfo& info=*param->info;
            if(!std::isfinite(number)||number<info.min||number>info.max)
                return fail(why,[NSString stringWithFormat:@"参数超出范围：%@",rawKey]);
            if(param->discrete && number!=std::trunc(number))
                return fail(why,[NSString stringWithFormat:@"参数必须为整数：%@",rawKey]);
            node.parameters[pid]=float(number);
        }
        if(node.type==tri::ArrangerNode && !tri::validArrangerRanges(node))
            return fail(why,@"同一声部的最低音不能高于最高音。");
        if(obj[@"asset"]) {
            const char* kind=spec->assetKind;
            if(!kind)return fail(why,@"该节点类型不能包含素材。");
            if(decodeAssets) {
                if(std::string(kind)=="audio") {
                    std::shared_ptr<tri::AudioSample> audio;
                    if(!parseAudioAsset(obj[@"asset"],audio,why))return false;
                    node.sample=std::move(audio);
                } else if(!decode(@{[NSString stringWithUTF8String:kind]:obj[@"asset"]},
                                   node.clip,node.model,why)) {
                    return fail(why,@"节点素材无效。");
                }
            }
        }
        if(!obj[@"asset"] && spec->assetKind && decodeAssets)
            early.push_back({node.id,"素材缺失，节点输出保持静音。",false});
        next.nodes.push_back(std::move(node));
    }
    for(id edge in document[@"edges"]) {
        if(![edge isKindOfClass:NSDictionary.class] || !integer(edge[@"source"],1,9007199254740991.) ||
            !integer(edge[@"target"],1,9007199254740991.))return fail(why,@"连线格式无效。");
        tri::GraphEdge parsed;
        parsed.source=[edge[@"source"] unsignedLongLongValue];
        parsed.target=[edge[@"target"] unsignedLongLongValue];
        // Ports may be named (v4) or positional (v3). A named port is resolved
        // through the descriptor; an unknown name is an error, never a silent 0.
        const tri::GraphNode* target=nullptr;
        const tri::GraphNode* source=nullptr;
        for(const auto& node:next.nodes) {
            if(node.id==parsed.target)target=&node;
            if(node.id==parsed.source)source=&node;
        }
        if(!target||!source)return fail(why,@"连线引用了不存在的节点。");
        if(target->unavailable||source->unavailable) {
            if([edge[@"port"] isKindOfClass:NSString.class])parsed.portId=[edge[@"port"] UTF8String] ?: "";
            if([edge[@"sourcePort"] isKindOfClass:NSString.class])parsed.sourcePortId=[edge[@"sourcePort"] UTF8String] ?: "";
            next.edges.push_back(std::move(parsed));
            continue;
        }
        id port=edge[@"port"],sourcePort=edge[@"sourcePort"];
        if([port isKindOfClass:NSString.class]) {
            parsed.portId=[port UTF8String] ?: "";
            parsed.port=tri::nodePortIndex(target->type,true,parsed.portId.c_str());
            if(parsed.port<0)return fail(why,[NSString stringWithFormat:@"未知的输入端口：%@",port]);
        } else if(numeric(port)) {
            parsed.port=[port intValue];
            if(parsed.port<0||parsed.port>=tri::nodeInputCount(target->type))
                return fail(why,@"连线指向不存在的输入端口。");
        } else {
            parsed.port=0;
        }
        if([sourcePort isKindOfClass:NSString.class]) {
            parsed.sourcePortId=[sourcePort UTF8String] ?: "";
            parsed.sourcePort=tri::nodePortIndex(source->type,false,parsed.sourcePortId.c_str());
            if(parsed.sourcePort<0)return fail(why,[NSString stringWithFormat:@"未知的输出端口：%@",sourcePort]);
        } else if(numeric(sourcePort)) {
            parsed.sourcePort=[sourcePort intValue];
            if(parsed.sourcePort<0||parsed.sourcePort>=tri::nodeOutputCount(source->type))
                return fail(why,@"连线引用了不存在的输出端口。");
        } else {
            parsed.sourcePort=0;
        }
        next.edges.push_back(std::move(parsed));
    }
    tri::CompiledGraph plan;
    std::string reason;
    if(!tri::compileGraph(next,plan,&reason,allowUnknownNodes))
        return fail(why,reason.empty()?@"工作流图无效。":[NSString stringWithUTF8String:reason.c_str()]);
    if(!early.empty())plan.diagnostics.insert(plan.diagnostics.begin(),early.begin(),early.end());
    if(diagnostics)*diagnostics=plan.diagnostics;
    graph=std::move(next);
    return true;
}

NSDictionary *canonicalGraph(NSDictionary *document, NSString **why) {
    if(![document isKindOfClass:NSDictionary.class]) {
        fail(why,@"工作流应为对象。");
        return nil;
    }
    if([document[@"version"] intValue]==4) {
        // Already canonical; still validate so a corrupt v4 file never reaches
        // the engine through the editor.
        tri::GraphSpec graph;
        if(!decodeGraph(document,graph,why,false,true))return nil;
        return document;
    }
    tri::GraphSpec graph;
    if(!decodeGraph(document,graph,why,false,true))return nil;
    NSMutableArray *nodes=[NSMutableArray arrayWithCapacity:graph.nodes.size()];
    for(const auto& node:graph.nodes) {
        NSMutableDictionary *row=[NSMutableDictionary new];
        row[@"id"]=@(node.id);
        if(node.unavailable) {
            row[@"nodeKey"]=[NSString stringWithUTF8String:node.key.c_str()];
            row[@"unavailable"]=@YES;
        } else {
            const tri::NodeSpec *spec=tri::nodeSpec(node.type);
            row[@"nodeKey"]=[NSString stringWithUTF8String:spec->key];
            row[@"nodeVersion"]=@(node.nodeVersion);
            row[@"stateVersion"]=@(spec->stateVersion);
            NSMutableDictionary *parameters=[NSMutableDictionary new];
            for(const auto& [pid,value]:node.parameters)
                parameters[[NSString stringWithUTF8String:tri::paramKey(pid)]]=@(value);
            row[@"parameters"]=parameters;
        }
        row[@"x"]=@(node.x);row[@"y"]=@(node.y);
        if(node.bypass)row[@"bypass"]=@YES;
        // Re-attach the original asset payload untouched.
        for(id original in document[@"nodes"]) {
            if([original isKindOfClass:NSDictionary.class] &&
               [original[@"id"] unsignedLongLongValue]==node.id && original[@"asset"])
                row[@"asset"]=original[@"asset"];
        }
        [nodes addObject:row];
    }
    NSMutableArray *edges=[NSMutableArray arrayWithCapacity:graph.edges.size()];
    for(const auto& edge:graph.edges) {
        NSMutableDictionary *row=[NSMutableDictionary new];
        row[@"source"]=@(edge.source);row[@"target"]=@(edge.target);
        const tri::GraphNode* source=nullptr;
        for(const auto& node:graph.nodes)if(node.id==edge.source)source=&node;
        std::string sourcePort=edge.sourcePortId;
        if(sourcePort.empty()&&source&&!source->unavailable)
            sourcePort=tri::nodePortId(source->type,false,size_t(edge.sourcePort));
        std::string port=edge.portId;
        if(port.empty()) {
            for(const auto& node:graph.nodes)
                if(node.id==edge.target&&!node.unavailable)
                    port=tri::nodePortId(node.type,true,size_t(edge.port));
        }
        if(!sourcePort.empty())row[@"sourcePort"]=[NSString stringWithUTF8String:sourcePort.c_str()];
        if(!port.empty())row[@"port"]=[NSString stringWithUTF8String:port.c_str()];
        [edges addObject:row];
    }
    NSMutableDictionary *result=[NSMutableDictionary new];
    result[@"version"]=@4;
    result[@"projectId"]=document[@"projectId"] ?: @"local";
    result[@"nodes"]=nodes;
    result[@"edges"]=edges;
    if([document[@"groups"] isKindOfClass:NSArray.class])result[@"groups"]=document[@"groups"];
    if([document[@"settings"] isKindOfClass:NSDictionary.class])result[@"settings"]=document[@"settings"];
    return result;
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
