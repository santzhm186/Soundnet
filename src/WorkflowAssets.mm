#import "WorkflowAssets.h"
#import <CoreML/CoreML.h>
#import <AVFoundation/AVFoundation.h>
#import <CommonCrypto/CommonDigest.h>
#include "StandardMidiFile.hpp"
#include "AudioFile.hpp"
#include "WavFile.hpp"
#include <climits>
#include <set>
#include <cmath>
#include <cstring>
#include <memory>

namespace triassets {
namespace {
// A single text parameter (prompt, lyrics) and the sum per node are bounded so
// a crafted project cannot balloon memory during decode.
constexpr size_t maxTextBytes=256*1024;
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
bool parseClip(NSData *bytes, tri::MidiClip& clip, NSString **why, bool performance=false) {
    try {
        clip=tri::smf::parse(static_cast<const uint8_t *>(bytes.bytes),bytes.length,performance);
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

static bool decodeAssetMap(NSDictionary *assets, tri::MidiClip& clip, tri::NoteModel& model,
                         NSString **why, bool performance) {
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
        if (!parseClip(data,nextClip,why,performance)) return false;
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
bool decode(NSDictionary *assets, tri::MidiClip& clip, tri::NoteModel& model, NSString **why) {
    return decodeAssetMap(assets,clip,model,why,false);
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
        if(value!=std::trunc(value)||value<1||value>INT_MAX)return fail(why,@"节点类型编号无效。");
        const tri::NodeSpec *spec=tri::nodeSpec(int(value));
        if(!spec||!spec->instantiable)return fail(why,@"节点类型编号无效或不可实例化。");
        type=int(value);return true;
    }
    return fail(why,@"节点缺少 nodeKey。");
}
// A v4 parameter key is the stable alias; a v3 key is the numeric ID. Both are
// accepted so migration never depends on field order.
int documentParamID(NSString *key,int type,bool strict,NSString **why) {
    const int byKey=tri::nodeParamIdForKey(type,key.UTF8String);
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
                 bool allowUnknownNodes, std::vector<tri::Diagnostic>* diagnostics,
                 const tri::GraphSpec* preparedGraph, NSDictionary* preparedDocument) {
    if(![document isKindOfClass:NSDictionary.class] || ![document[@"nodes"] isKindOfClass:NSArray.class] ||
       ![document[@"edges"] isKindOfClass:NSArray.class])
        return fail(why,@"请选择 Soundnet 工作流文件。");
    id rawVersion=document[@"version"];
    if(!numeric(rawVersion))
        return fail(why,@"请选择版本 3 或 4 的 Soundnet 工作流。");
    const int version=[rawVersion intValue];
    if((version!=3 && version!=4) || [rawVersion doubleValue]!=version)
        return fail(why,@"请选择版本 3 或 4 的 Soundnet 工作流。");
    const bool strict=version>=4;
    auto integer=[](id v,double min,double max) {
        return numeric(v) && std::isfinite([v doubleValue]) && [v doubleValue]>=min &&
            [v doubleValue]<=max && std::trunc([v doubleValue])==[v doubleValue];
    };
    tri::GraphSpec next;
    if([document[@"settings"] isKindOfClass:NSDictionary.class]) {
        id required=document[@"settings"][@"environmentRequired"];
        if(required && ![required isKindOfClass:NSNumber.class])return fail(why,@"工程环境要求应为布尔值。");
        next.environmentRequired=[required boolValue];
    }
    std::vector<tri::Diagnostic> early;
    // Resource reuse is per instance, independent of edits to unrelated nodes.
    // The document comparison protects against replacing an asset in place.
    std::map<tri::InstanceID,const tri::GraphNode*> preparedNodes;
    NSMutableDictionary *preparedRows=[NSMutableDictionary new];
    if(preparedGraph && [preparedDocument isKindOfClass:NSDictionary.class]) {
        for(const auto& old:preparedGraph->nodes)preparedNodes.emplace(old.id,&old);
        if([preparedDocument[@"nodes"] isKindOfClass:NSArray.class])
            for(id row in preparedDocument[@"nodes"])
                if([row isKindOfClass:NSDictionary.class] && numeric(row[@"id"]))
                    preparedRows[row[@"id"]]=row;
    }
    if([document[@"projectId"] isKindOfClass:NSString.class])
        next.projectId=[document[@"projectId"] UTF8String] ?: "";
    if([document[@"nodes"] count]>tri::maxGraphNodes)return fail(why,@"节点数量超过上限。");
    if([document[@"edges"] count]>tri::maxGraphEdges)return fail(why,@"连线数量超过上限。");
    for(id obj in document[@"nodes"]) {
        if(![obj isKindOfClass:NSDictionary.class])return fail(why,@"节点格式无效。");
        NSDictionary *previousRow=numeric(obj[@"id"])?preparedRows[obj[@"id"]]:nil;
        const bool reusedResources=previousRow&&[obj[@"resources"] isEqual:previousRow[@"resources"]];
        if(!tri::projectresources::validate(obj,why,decodeAssets&&!reusedResources))return false;
        if(!integer(obj[@"id"],1,9007199254740991.) || !numeric(obj[@"x"]) || !numeric(obj[@"y"]) ||
           !std::isfinite([obj[@"x"] doubleValue]) || !std::isfinite([obj[@"y"] doubleValue]))
            return fail(why,@"节点的实例 ID 或坐标无效。");
        id parameters=obj[@"parameters"] ?: @{};
        if(![parameters isKindOfClass:NSDictionary.class])return fail(why,@"节点参数应为对象。");
        tri::GraphNode node;
        node.id=[obj[@"id"] unsignedLongLongValue];
        node.x=[obj[@"x"] floatValue];node.y=[obj[@"y"] floatValue];
        if(obj[@"nodeVersion"] && !integer(obj[@"nodeVersion"],1,UINT16_MAX))
            return fail(why,@"节点行为版本必须为 1–65535 的整数。");
        if(obj[@"stateVersion"] && !integer(obj[@"stateVersion"],1,UINT8_MAX))
            return fail(why,@"节点状态版本必须为 1–255 的整数。");
        node.nodeVersion=obj[@"nodeVersion"]?[obj[@"nodeVersion"] unsignedIntValue]:1;
        node.stateVersion=obj[@"stateVersion"]?[obj[@"stateVersion"] unsignedIntValue]:1;
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
        if(node.nodeVersion!=spec->nodeVersion || node.stateVersion!=spec->stateVersion) {
            // Early development has no implicit migration promise. A version
            // the module does not support is retained as an inert placeholder.
            const std::string message=std::string("节点 ")+spec->key+" 的行为版本或状态版本不受当前模块支持。";
            if(!allowUnknownNodes)return fail(why,[NSString stringWithUTF8String:message.c_str()]);
            early.push_back({node.id,message,false});
            node.unavailable=true;node.type=0;node.unavailableReason=message;
            next.nodes.push_back(std::move(node));
            continue;
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
            // Graph parameters are float. Comparing a decimal literal such as
            // 0.001 against a float minimum rejects a legal endpoint, so both
            // sides are brought to float before the range check.
            const float stored=float(number);
            const tri::ParamInfo& info=*param->info;
            if(!std::isfinite(number)||!std::isfinite(stored)||stored<info.min||stored>info.max)
                return fail(why,[NSString stringWithFormat:@"参数超出范围：%@",rawKey]);
            if(param->discrete && number!=std::trunc(number))
                return fail(why,[NSString stringWithFormat:@"参数必须为整数：%@",rawKey]);
            node.parameters[pid]=stored;
        }
        // Text parameters: stable string keys declared by the descriptor. v3 has
        // no such concept; v4 rejects keys the node never declared so a typo
        // cannot silently create a parameter the inspector will never show.
        if(obj[@"texts"]) {
            id texts=obj[@"texts"];
            if(![texts isKindOfClass:NSDictionary.class])return fail(why,@"节点文本参数应为对象。");
            size_t total=0;
            for(id rawKey in texts) {
                if(![rawKey isKindOfClass:NSString.class])return fail(why,@"文本参数标识无效。");
                id value=texts[rawKey];
                if(![value isKindOfClass:NSString.class])return fail(why,@"文本参数值应为字符串。");
                const char* key=[rawKey UTF8String];
                if(!key||!tri::nodeTextSpec(node.type,key)) {
                    if(strict)return fail(why,[NSString stringWithFormat:@"文本参数不属于该节点：%@",rawKey]);
                    continue;
                }
                const char* text=[value UTF8String];
                if(!text)return fail(why,@"文本参数不是有效的 UTF-8。");
                const size_t length=std::strlen(text);
                if(length>maxTextBytes)return fail(why,@"文本参数过长。");
                total+=length;
                if(total>maxTextBytes*4)return fail(why,@"文本参数总长度超限。");
                node.texts[key]=text;
            }
        }
        if(obj[@"asset"]) {
            const char* kind=spec->assetKind;
            if(!kind)return fail(why,@"该节点类型不能包含素材。");
            if(decodeAssets) {
                auto previous=preparedNodes.find(node.id);
                NSDictionary *oldRow=preparedRows[obj[@"id"]];
                const bool same=previous!=preparedNodes.end() && previous->second->type==node.type &&
                    !previous->second->unavailable && oldRow &&
                    [obj[@"asset"] isEqual:oldRow[@"asset"]];
                if(same) {
                    node.sample=previous->second->sample;
                    node.clip=previous->second->clip;
                    node.model=previous->second->model;
                } else if(std::string(kind)=="audio") {
                    std::shared_ptr<tri::AudioSample> audio;
                    if(!parseAudioAsset(obj[@"asset"],audio,why))return false;
                    node.sample=std::move(audio);
                } else if(!decodeAssetMap(@{[NSString stringWithUTF8String:kind]:obj[@"asset"]},
                                         node.clip,node.model,why,true)) {
                    return fail(why,@"节点素材无效。");
                }
            }
        }
        if(!obj[@"asset"] && spec->assetKind && decodeAssets)
            early.push_back({node.id,"素材缺失，节点输出保持静音。",false});
        next.nodes.push_back(std::move(node));
    }
    std::map<uint64_t,const tri::GraphNode*> nodeById;
    for(const auto& node:next.nodes) nodeById.emplace(node.id,&node);
    // Non-trigger inputs are single-source. v3 documents may still carry two
    // edges into one port because the old loader silently kept the last one;
    // keep that edge and drop the earlier copies so the project still opens.
    std::set<std::pair<uint64_t,int>> occupiedInputs;
    for(id edge in document[@"edges"]) {
        if(![edge isKindOfClass:NSDictionary.class] || !integer(edge[@"source"],1,9007199254740991.) ||
            !integer(edge[@"target"],1,9007199254740991.))return fail(why,@"连线格式无效。");
        tri::GraphEdge parsed;
        parsed.source=[edge[@"source"] unsignedLongLongValue];
        parsed.target=[edge[@"target"] unsignedLongLongValue];
        // Ports may be named (v4) or positional (v3). A named port is resolved
        // through the descriptor; an unknown name is an error, never a silent 0.
        auto foundTarget=nodeById.find(parsed.target),foundSource=nodeById.find(parsed.source);
        const tri::GraphNode* target=foundTarget==nodeById.end()?nullptr:foundTarget->second;
        const tri::GraphNode* source=foundSource==nodeById.end()?nullptr:foundSource->second;
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
            if(!integer(port,0,INT_MAX))return fail(why,@"输入端口序号必须为非负整数。");
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
            if(!integer(sourcePort,0,INT_MAX))return fail(why,@"输出端口序号必须为非负整数。");
            parsed.sourcePort=[sourcePort intValue];
            if(parsed.sourcePort<0||parsed.sourcePort>=tri::nodeOutputCount(source->type))
                return fail(why,@"连线引用了不存在的输出端口。");
        } else {
            parsed.sourcePort=0;
        }
        const tri::PortSpec* inputPort=tri::nodePort(target->type,true,size_t(parsed.port));
        const bool trigger=inputPort && (inputPort->role==tri::RoleTrigger||
                                         inputPort->signal==tri::Signal::Trigger);
        if(!trigger) {
            auto key=std::make_pair(parsed.target,parsed.port);
            if(!occupiedInputs.insert(key).second) {
                if(strict)return fail(why,@"同一输入端口只能有一条连接。");
                for(size_t i=0;i<next.edges.size();++i)
                    if(next.edges[i].target==parsed.target&&next.edges[i].port==parsed.port) {
                        next.edges.erase(next.edges.begin()+long(i));
                        break;
                    }
                early.push_back({parsed.target,"忽略重复连接，保留最后一条。",false});
            }
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
    if(numeric(document[@"version"]) && [document[@"version"] intValue]==4) {
        // Already canonical; still validate so a corrupt v4 file never reaches
        // the engine through the editor.
        tri::GraphSpec graph;
        if(!decodeGraph(document,graph,why,false,true))return nil;
        return document;
    }
    tri::GraphSpec graph;
    if(!decodeGraph(document,graph,why,false,true))return nil;
    // Rebuild from the original dictionaries so a custom `name`, an asset and
    // any field this build does not know about survive the migration.
    std::map<uint64_t,NSDictionary*> originalById;
    for(id row in document[@"nodes"])
        if([row isKindOfClass:NSDictionary.class] && numeric(row[@"id"]))
            originalById.emplace([row[@"id"] unsignedLongLongValue],row);
    NSMutableArray *nodes=[NSMutableArray arrayWithCapacity:graph.nodes.size()];
    for(const auto& node:graph.nodes) {
        auto original=originalById.find(node.id);
        NSMutableDictionary *row=original!=originalById.end()
            ? [original->second mutableCopy] : [NSMutableDictionary new];
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
            if(!node.texts.empty()) {
                NSMutableDictionary *texts=[NSMutableDictionary new];
                for(const auto& [key,value]:node.texts)
                    texts[[NSString stringWithUTF8String:key.c_str()]]=
                        [NSString stringWithUTF8String:value.c_str()] ?: @"";
                row[@"texts"]=texts;
            } else {
                [row removeObjectForKey:@"texts"];
            }
        }
        row[@"x"]=@(node.x);row[@"y"]=@(node.y);
        if(node.bypass)row[@"bypass"]=@YES;
        else [row removeObjectForKey:@"bypass"];
        [nodes addObject:row];
    }
    NSMutableArray *edges=[NSMutableArray arrayWithCapacity:graph.edges.size()];
    std::map<uint64_t,const tri::GraphNode*> byId;
    for(const auto& node:graph.nodes) byId.emplace(node.id,&node);
    for(const auto& edge:graph.edges) {
        NSMutableDictionary *row=[NSMutableDictionary new];
        row[@"source"]=@(edge.source);row[@"target"]=@(edge.target);
        auto foundSource=byId.find(edge.source);
        const tri::GraphNode* source=foundSource==byId.end()?nullptr:foundSource->second;
        std::string sourcePort=edge.sourcePortId;
        if(sourcePort.empty()&&source&&!source->unavailable)
            sourcePort=tri::nodePortId(source->type,false,size_t(edge.sourcePort));
        std::string port=edge.portId;
        if(port.empty()) {
            auto foundTarget=byId.find(edge.target);
            if(foundTarget!=byId.end()&&!foundTarget->second->unavailable)
                port=tri::nodePortId(foundTarget->second->type,true,size_t(edge.port));
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
            if (!data || !parseClip(data,clip,why,true)) return nil;
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

namespace {
NSString *assetKind(NSDictionary *asset) {
    if(asset[@"pcm"])return @"audio";
    if(asset[@"smf"])return @"clip";
    if(asset[@"pitches"])return @"model";
    return nil;
}

NSData *fileForAsset(NSDictionary *asset,NSString *kind,NSString **extension,NSString **why) {
    if([kind isEqual:@"audio"]) {
        std::shared_ptr<tri::AudioSample> sample;
        if(!parseAudioAsset(asset,sample,why))return nil;
        *extension=@"wav";
        const uint16_t stride=uint16_t(sample->channels*sizeof(float));
        const uint32_t rate=uint32_t(std::lround(sample->rate));
        const uint32_t bytes=uint32_t(sample->samples.size()*sizeof(float));
        std::vector<uint8_t> header{'R','I','F','F'};
        tri::wav::put32(header,36+bytes);
        header.insert(header.end(),{'W','A','V','E','f','m','t',' '});
        tri::wav::put32(header,16);
        tri::wav::put16(header,3);
        tri::wav::put16(header,uint16_t(sample->channels));
        tri::wav::put32(header,rate);
        tri::wav::put32(header,rate*stride);
        tri::wav::put16(header,stride);
        tri::wav::put16(header,32);
        header.insert(header.end(),{'d','a','t','a'});
        tri::wav::put32(header,bytes);
        NSMutableData *wav=[NSMutableData dataWithBytes:header.data() length:header.size()];
        [wav appendBytes:sample->samples.data() length:bytes];
        return wav;
    }
    if([kind isEqual:@"clip"]) {
        *extension=@"mid";
        if(![asset[@"smf"] isKindOfClass:NSString.class]) {
            fail(why,@"MIDI 素材编码无效。");return nil;
        }
        NSData *data=[[NSData alloc] initWithBase64EncodedString:asset[@"smf"] options:0];
        if(!data.length)fail(why,@"MIDI 素材编码无效。");
        tri::MidiClip clip;
        return data.length&&parseClip(data,clip,why,true)?data:nil;
    }
    *extension=@"json";
    tri::NoteModel model;
    if(!parsePitches(asset[@"pitches"],model,why))return nil;
    return [NSJSONSerialization dataWithJSONObject:@{@"pitches":asset[@"pitches"]} options:0 error:nil];
}

NSURL *resolveProjectAsset(NSURL *directory,NSString *path,NSString **why) {
    if(![path isKindOfClass:NSString.class]||!path.length||path.length>4096||path.isAbsolutePath) {
        fail(why,@"工程素材引用须为相对路径。");return nil;
    }
    for(NSString *component in [path componentsSeparatedByString:@"/"]) {
        if(!component.length||[component isEqual:@"."]||[component isEqual:@".."]) {
            fail(why,@"工程素材引用不能越过工程目录。");return nil;
        }
    }
    NSString *root=directory.path.stringByResolvingSymlinksInPath;
    NSString *prefix=[root hasSuffix:@"/"]?root:[root stringByAppendingString:@"/"];
    NSURL *asset=[directory URLByAppendingPathComponent:path isDirectory:NO];
    if(![asset.path.stringByResolvingSymlinksInPath hasPrefix:prefix]) {
        fail(why,@"工程素材引用不能越过工程目录。");return nil;
    }
    return asset;
}

NSString *storeProjectAsset(NSData *data,NSString *extension,NSURL *directory,NSString *folder,NSString **why) {
    if(!data)return nil;
    unsigned char digest[CC_SHA256_DIGEST_LENGTH]{};
    CC_SHA256(data.bytes,CC_LONG(data.length),digest);
    NSMutableString *name=[NSMutableString string];
    for(unsigned char byte:digest)[name appendFormat:@"%02x",unsigned(byte)];
    NSString *path=[folder stringByAppendingPathComponent:[name stringByAppendingPathExtension:extension]];
    NSURL *destination=resolveProjectAsset(directory,path,why);
    if(!destination)return nil;
    NSFileManager *manager=NSFileManager.defaultManager;
    NSError *error=nil;
    if(![manager createDirectoryAtURL:destination.URLByDeletingLastPathComponent
              withIntermediateDirectories:YES attributes:nil error:&error]) {
        fail(why,error.localizedDescription);return nil;
    }
    if([manager fileExistsAtPath:destination.path]) {
        NSData *existing=[NSData dataWithContentsOfURL:destination options:NSDataReadingMappedIfSafe error:nil];
        if([existing isEqualToData:data])return path;
        NSString *alternate=[NSUUID.UUID.UUIDString stringByAppendingPathExtension:extension];
        path=[folder stringByAppendingPathComponent:alternate];
        destination=resolveProjectAsset(directory,path,why);
        if(!destination)return nil;
    }
    if(![data writeToURL:destination options:NSDataWritingAtomic error:&error]) {
        fail(why,error.localizedDescription);return nil;
    }
    return path;
}

const tri::TextSpec *projectTextSpec(NSDictionary *node,NSString *key) {
    if(![node[@"nodeKey"] isKindOfClass:NSString.class]||![key isKindOfClass:NSString.class])return nullptr;
    int type=tri::nodeTypeForKey([node[@"nodeKey"] UTF8String]);
    return tri::nodeTextSpec(type,key.UTF8String);
}

NSDictionary *writeResource(NSDictionary *resource,NSURL *directory,NSString *folder,NSString **why) {
    NSString *kind=resource[@"kind"],*extension=nil;
    NSData *bytes=nil;
    if([kind isEqual:@"text"]) {
        extension=resource[@"extension"];
        bytes=[resource[@"text"] dataUsingEncoding:NSUTF8StringEncoding];
    } else bytes=fileForAsset(resource[@"data"],kind,&extension,why);
    NSString *path=storeProjectAsset(bytes,extension,directory,folder,why);
    if(!path)return nil;
    return @{@"kind":kind,@"name":resource[@"name"],@"path":path,@"extension":extension};
}
NSDictionary *readResource(NSDictionary *ref,NSURL *directory,NSString **why) {
    if(!named(ref)||![@[@"audio",@"clip",@"model",@"text"] containsObject:ref[@"kind"] ?: NSNull.null]) {
        fail(why,@"工程素材引用格式无效。");return nil;
    }
    NSURL *url=resolveProjectAsset(directory,ref[@"path"],why);
    if(!url)return nil;
    NSString *kind=ref[@"kind"],*extension=[kind isEqual:@"text"]?ref[@"extension"]:
        @{@"audio":@"wav",@"clip":@"mid",@"model":@"json"}[kind];
    if(![extension isKindOfClass:NSString.class]||!extension.length||
       ![url.pathExtension.lowercaseString isEqual:extension]) {
        fail(why,[NSString stringWithFormat:@"工程素材类型与文件扩展名不符：%@",ref[@"path"]]);return nil;
    }
    if([kind isEqual:@"text"]) {
        NSData *bytes=readLimited(url,maxTextBytes,nullptr);
        NSString *value=bytes?[[NSString alloc]initWithData:bytes encoding:NSUTF8StringEncoding]:nil;
        if(!value) {fail(why,[NSString stringWithFormat:@"无法读取工程文本素材 %@。",ref[@"path"]]);return nil;}
        return tri::projectresources::textResource(ref[@"name"],extension,value);
    }
    NSString *error=nil;
    NSMutableDictionary *asset=[importAsset(kind,url,&error) mutableCopy];
    if(!asset) {
        fail(why,[NSString stringWithFormat:@"无法读取工程素材 %@。%@",ref[@"path"],error ?: @""]);return nil;
    }
    asset[@"name"]=ref[@"name"];
    return tri::projectresources::assetResource(kind,asset);
}
// v5's former generation log stored external file paths in text. Convert that
// old platform convention once on file import; new nodes never create it.
NSDictionary *upgradeLegacyTakeRecords(NSDictionary *document,NSString **why) {
    if(![document[@"nodes"] isKindOfClass:NSArray.class])return document;
    NSMutableArray *nodes=[NSMutableArray new];bool changed=false;
    for(id raw in document[@"nodes"]) {
        if(![raw isKindOfClass:NSDictionary.class])return document;
        NSDictionary *row=raw;
        id texts=row[@"texts"];
        id encoded=[texts isKindOfClass:NSDictionary.class]?texts[@"gen.takes"]:nil;
        if(encoded) {
            if(![encoded isKindOfClass:NSString.class]||
               [encoded lengthOfBytesUsingEncoding:NSUTF8StringEncoding]>maxTextBytes) {
                fail(why,@"旧生成记录格式无效或超过容量。");return nil;
            }
            NSData *data=[encoded dataUsingEncoding:NSUTF8StringEncoding];
            id takes=[encoded length]?[NSJSONSerialization JSONObjectWithData:data options:0 error:nil]:@[];
            if(![takes isKindOfClass:NSArray.class]||[takes count]>1024) {fail(why,@"旧生成记录格式无效。");return nil;}
            for(NSUInteger index=0;index<[takes count];++index) {
                NSDictionary *take=takes[index];
                if(![take isKindOfClass:NSDictionary.class]) {fail(why,@"旧生成记录格式无效。");return nil;}
                NSMutableDictionary *resources=[NSMutableDictionary new];
                NSString *path=[take[@"file"] isKindOfClass:NSString.class]?take[@"file"]:@"";
                if(path.length) {
                    NSString *error=nil;
                    NSDictionary *asset=importAsset(@"audio",[NSURL fileURLWithPath:path],&error);
                    if(!asset) {fail(why,[NSString stringWithFormat:@"无法收集旧生成素材 %@。%@",path,error ?: @""]);return nil;}
                    resources[@"audio"]=tri::projectresources::assetResource(@"audio",asset);
                }
                // Old unarchived takes contained only metadata. Preserve the
                // final active result when available, never invent old audio.
                if(index+1==[takes count]) {
                    if(!resources[@"audio"]&&row[@"asset"])
                        resources[@"audio"]=tri::projectresources::assetResource(@"audio",row[@"asset"]);
                    for(NSString *key in texts) {
                        const auto* spec=projectTextSpec(row,key);
                        if(spec&&spec->assetExtension&&std::strcmp(spec->assetExtension,"abc")==0&&
                           [texts[key] isKindOfClass:NSString.class]&&[texts[key] length])
                            resources[@"score"]=tri::projectresources::textResource(@"Score",@"abc",texts[key]);
                    }
                }
                NSMutableDictionary *metadata=[take mutableCopy];
                [metadata removeObjectForKey:@"file"];[metadata removeObjectForKey:@"name"];
                metadata[@"unavailable"]=@(resources.count==0);
                row=tri::projectresources::appendEntry(row,@"generation.takes",
                    [take[@"name"] isKindOfClass:NSString.class]?take[@"name"]:@"Take",resources,metadata);
            }
            NSMutableDictionary *replacement=[row mutableCopy],*updatedTexts=[texts mutableCopy];
            [updatedTexts removeObjectForKey:@"gen.takes"];replacement[@"texts"]=updatedTexts;
            row=replacement;changed=true;
        }
        [nodes addObject:row];
    }
    if(!changed)return document;
    NSMutableDictionary *result=[document mutableCopy];result[@"nodes"]=nodes;return result;
}
} // namespace

NSURL *autosaveURL() {
    NSURL *base=[NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory
                                                   inDomains:NSUserDomainMask].firstObject;
    return [[base URLByAppendingPathComponent:@"Soundnet" isDirectory:YES]
                 URLByAppendingPathComponent:@"Autosave.json" isDirectory:NO];
}

bool writeProject(NSDictionary *document,NSURL *url,NSString **why) {
    if(!url.isFileURL)return fail(why,@"请选择本机工程文件。");
    NSDictionary *graph=canonicalGraph(document,why);
    if(!graph)return false;
    NSURL *directory=url.URLByDeletingLastPathComponent;
    NSString *folder=[url.lastPathComponent.stringByDeletingPathExtension stringByAppendingString:@".assets"];
    NSMutableArray *nodes=[NSMutableArray new];
    for(NSDictionary *node in graph[@"nodes"]) {
        NSMutableDictionary *row=[node mutableCopy];
        if(row[@"assetRef"]||row[@"textRefs"]||row[@"resourceRefs"])return fail(why,@"保存前须先解析工程素材引用。");
        if(row[@"asset"]) {
            NSDictionary *asset=row[@"asset"];
            if(!named(asset))return fail(why,@"工程包含无效素材。");
            NSString *kind=assetKind(asset),*extension=nil;
            if(!kind)return fail(why,@"工程包含无法另存的素材类型。");
            NSData *data=fileForAsset(asset,kind,&extension,why);
            NSString *path=storeProjectAsset(data,extension,directory,folder,why);
            if(!path)return false;
            row[@"assetRef"]=@{@"kind":kind,@"path":path,@"name":asset[@"name"]};
            [row removeObjectForKey:@"asset"];
        }
        if([row[@"texts"] isKindOfClass:NSDictionary.class]) {
            NSMutableDictionary *texts=[row[@"texts"] mutableCopy],*refs=[NSMutableDictionary new];
            for(NSString *key in texts.allKeys) {
                const auto* spec=projectTextSpec(row,key);
                NSString *extension=[row[@"textResourceTypes"] isKindOfClass:NSDictionary.class]
                    ?row[@"textResourceTypes"][key]:nil;
                if(!extension&&spec&&spec->assetExtension)extension=@(spec->assetExtension);
                NSString *value=texts[key];
                if(!extension||![value isKindOfClass:NSString.class]||!value.length)continue;
                NSDictionary *ref=writeResource(tri::projectresources::textResource(key,extension,value),directory,folder,why);
                if(!ref)return false;
                refs[key]=ref;
                [texts removeObjectForKey:key];
            }
            if(refs.count) {row[@"texts"]=texts;row[@"textRefs"]=refs;}
        }
        if(row[@"resources"]) {
            NSMutableDictionary *refs=[NSMutableDictionary new];
            for(NSString *key in row[@"resources"]) {
                NSDictionary *ref=writeResource(row[@"resources"][key],directory,folder,why);
                if(!ref)return false;
                refs[key]=ref;
            }
            row[@"resourceRefs"]=refs;[row removeObjectForKey:@"resources"];
        }
        [row removeObjectForKey:@"textResourceTypes"];
        [nodes addObject:row];
    }
    NSMutableDictionary *manifest=[graph mutableCopy];
    manifest[@"version"]=@6;
    manifest[@"nodes"]=nodes;
    NSError *error=nil;
    if(![NSFileManager.defaultManager createDirectoryAtURL:directory
               withIntermediateDirectories:YES attributes:nil error:&error])
        return fail(why,error.localizedDescription);
    NSData *json=[NSJSONSerialization dataWithJSONObject:manifest
                               options:NSJSONWritingPrettyPrinted error:&error];
    return (json&&[json writeToURL:url options:NSDataWritingAtomic error:&error]) ||
           fail(why,error.localizedDescription ?: @"无法保存工程文件。");
}

NSDictionary *readProject(NSURL *url,NSString **why) {
    if(!url.isFileURL) {fail(why,@"请选择本机工程文件。");return nil;}
    NSError *error=nil;
    NSData *json=[NSData dataWithContentsOfURL:url options:NSDataReadingMappedIfSafe error:&error];
    id manifest=json?[NSJSONSerialization JSONObjectWithData:json options:0 error:&error]:nil;
    if(![manifest isKindOfClass:NSDictionary.class]) {
        fail(why,error.localizedDescription ?: @"工程文件无效。");return nil;
    }
    if(![manifest[@"version"] isEqual:@5]&&![manifest[@"version"] isEqual:@6])return upgradeLegacyTakeRecords(manifest,why);
    const bool v6=[manifest[@"version"] isEqual:@6];
    if(![manifest[@"nodes"] isKindOfClass:NSArray.class]||
       [manifest[@"nodes"] count]>tri::maxGraphNodes) {
        fail(why,@"工程节点格式无效。");return nil;
    }
    NSURL *directory=url.URLByDeletingLastPathComponent;
    NSMutableArray *nodes=[NSMutableArray new];
    for(id node in manifest[@"nodes"]) {
        if(![node isKindOfClass:NSDictionary.class]||node[@"asset"]||node[@"resources"]) {
            fail(why,@"工程节点须使用独立素材引用。");return nil;
        }
        NSMutableDictionary *row=[node mutableCopy];
        if(id ref=row[@"assetRef"]) {
            NSDictionary *resource=readResource(ref,directory,why);
            if(!resource)return nil;
            if([resource[@"kind"] isEqual:@"text"]) {
                fail(why,@"主素材引用不能使用文本资源。");return nil;
            }
            row[@"asset"]=resource[@"data"];
            [row removeObjectForKey:@"assetRef"];
        }
        if(id refs=row[@"textRefs"]) {
            if(![refs isKindOfClass:NSDictionary.class]||
               (row[@"texts"]&&![row[@"texts"] isKindOfClass:NSDictionary.class])) {
                fail(why,@"工程文本素材引用格式无效。");return nil;
            }
            NSMutableDictionary *texts=[row[@"texts"] mutableCopy] ?: [NSMutableDictionary new];
            NSMutableDictionary *types=[NSMutableDictionary new];
            for(NSString *key in refs) {
                if(![key isKindOfClass:NSString.class]||texts[key]) {
                    fail(why,@"工程文本素材引用与节点配置不符。");return nil;
                }
                id ref=refs[key];
                if(!v6&&[ref isKindOfClass:NSString.class])
                    ref=@{@"kind":@"text",@"name":key,@"path":ref,@"extension":[ref pathExtension].lowercaseString};
                NSDictionary *resource=readResource(ref,directory,why);
                if(!resource)return nil;
                if(![resource[@"kind"] isEqual:@"text"]) {
                    fail(why,@"文本素材引用必须使用文本资源。");return nil;
                }
                texts[key]=resource[@"text"];
                const auto* spec=projectTextSpec(row,key);
                if(!spec||!spec->assetExtension||![@(spec->assetExtension) isEqual:resource[@"extension"]])
                    types[key]=resource[@"extension"];
            }
            row[@"texts"]=texts;
            if(types.count)row[@"textResourceTypes"]=types;
            [row removeObjectForKey:@"textRefs"];
        }
        if(id refs=row[@"resourceRefs"]) {
            if(![refs isKindOfClass:NSDictionary.class]||[refs count]>1024) {
                fail(why,@"工程资源引用集合无效。");return nil;
            }
            NSMutableDictionary *resources=[NSMutableDictionary new];
            for(NSString *key in refs) {
                NSDictionary *resource=readResource(refs[key],directory,why);
                if(!resource)return nil;
                resources[key]=resource;
            }
            row[@"resources"]=resources;
            [row removeObjectForKey:@"resourceRefs"];
        }
        [nodes addObject:row];
    }
    NSMutableDictionary *resolved=[manifest mutableCopy];
    resolved[@"version"]=@4;
    resolved[@"nodes"]=nodes;
    NSDictionary *upgraded=upgradeLegacyTakeRecords(resolved,why);
    return upgraded?canonicalGraph(upgraded,why):nil;
}
} // namespace triassets

namespace tri::projectresources {
NSDictionary *importAsset(NSString *kind,NSURL *url,NSString **why) {
    return triassets::importAsset(kind,url,why);
}
NSDictionary *encodeClip(NSString *name,const MidiClip& clip,NSString **why) {
    return triassets::encodeClipAsset(name,clip,why);
}
bool decodeClip(NSDictionary *asset,MidiClip& clip,NSString **why) {
    if(![asset isKindOfClass:NSDictionary.class]) {
        if(why)*why=@"MIDI 素材无效。";return false;
    }
    NoteModel model;
    return triassets::decodeAssetMap(@{@"clip":asset},clip,model,why,true);
}
NSData *audioFileData(NSDictionary *asset,NSString **why) {
    NSString *extension=nil;
    return triassets::fileForAsset(asset,@"audio",&extension,why);
}
NSDictionary *assetResource(NSString *kind,NSDictionary *asset) {
    if(!kind||!asset)return nil;
    return @{@"kind":kind,@"name":asset[@"name"] ?: @"Resource",@"data":asset};
}
NSDictionary *textResource(NSString *name,NSString *extension,NSString *text) {
    if(!name||!extension||!text)return nil;
    return @{@"kind":@"text",@"name":name,@"extension":extension,@"text":text};
}
NSArray *collection(NSDictionary *node,NSString *key) {
    id collections=node[@"resourceCollections"];
    id result=[collections isKindOfClass:NSDictionary.class]?collections[key]:nil;
    return [result isKindOfClass:NSArray.class]?result:@[];
}
NSDictionary *entryResource(NSDictionary *node,NSDictionary *entry,NSString *role) {
    id refs=entry[@"resources"],resources=node[@"resources"];
    id key=[refs isKindOfClass:NSDictionary.class]?refs[role]:nil;
    id result=[resources isKindOfClass:NSDictionary.class]&&[key isKindOfClass:NSString.class]?resources[key]:nil;
    return [result isKindOfClass:NSDictionary.class]?result:nil;
}
NSDictionary *appendEntry(NSDictionary *node,NSString *collectionKey,NSString *name,
                          NSDictionary *resourcesByRole,NSDictionary *metadata) {
    NSMutableDictionary *result=[node mutableCopy];
    NSMutableDictionary *resources=[node[@"resources"] isKindOfClass:NSDictionary.class]
        ?[node[@"resources"] mutableCopy]:[NSMutableDictionary new];
    NSMutableDictionary *collections=[node[@"resourceCollections"] isKindOfClass:NSDictionary.class]
        ?[node[@"resourceCollections"] mutableCopy]:[NSMutableDictionary new];
    NSMutableDictionary *refs=[NSMutableDictionary new];
    for(NSString *role in resourcesByRole) {
        NSString *identifier=NSUUID.UUID.UUIDString;
        resources[identifier]=resourcesByRole[role];refs[role]=identifier;
    }
    NSMutableArray *entries=[collection(node,collectionKey) mutableCopy];
    [entries addObject:@{@"id":NSUUID.UUID.UUIDString,@"name":name ?: @"Resource",
        @"metadata":metadata ?: @{},@"resources":refs}];
    collections[collectionKey]=entries;
    result[@"resources"]=resources;result[@"resourceCollections"]=collections;
    return result;
}
static bool identifier(id value) {
    if(![value isKindOfClass:NSString.class]||![value length]||[value length]>160)return false;
    return [value rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"/\\\n\r"]].location==NSNotFound;
}
static bool extension(id value) {
    if(![value isKindOfClass:NSString.class]||![value length]||[value length]>16)return false;
    return [value rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:
        @"abcdefghijklmnopqrstuvwxyz0123456789"] invertedSet]].location==NSNotFound;
}
bool validate(NSDictionary *node,NSString **why,bool decodeResources) {
    auto invalid=[&](NSString *message){if(why)*why=message;return false;};
    id resources=node[@"resources"],collections=node[@"resourceCollections"];
    id textTypes=node[@"textResourceTypes"];
    if(textTypes&&(![textTypes isKindOfClass:NSDictionary.class]||[textTypes count]>128))
        return invalid(@"文本资源类型声明无效。");
    for(id key in textTypes) {
        id value=[node[@"texts"] isKindOfClass:NSDictionary.class]?node[@"texts"][key]:nil;
        if(!identifier(key)||!extension(textTypes[key])||![value isKindOfClass:NSString.class]||
           [value lengthOfBytesUsingEncoding:NSUTF8StringEncoding]>triassets::maxTextBytes)
            return invalid(@"文本资源类型或内容无效。");
    }
    if(resources&&(![resources isKindOfClass:NSDictionary.class]||[resources count]>1024))
        return invalid(@"节点资源集合格式无效或资源数量超过 1024。");
    size_t bytes=0;
    for(id key in resources) {
        NSDictionary *resource=resources[key];
        if(!identifier(key)||!triassets::named(resource))return invalid(@"工程资源 ID 或名称无效。");
        NSString *kind=resource[@"kind"];
        if([kind isEqual:@"text"]) {
            if(!extension(resource[@"extension"])||![resource[@"text"] isKindOfClass:NSString.class])
                return invalid(@"工程文本资源格式无效。");
            const NSUInteger length=[resource[@"text"] lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
            if(length>triassets::maxTextBytes)return invalid(@"工程文本资源超过容量。");
            bytes+=length;
        } else {
            if(![@[@"audio",@"clip",@"model"] containsObject:kind ?: NSNull.null]||!triassets::named(resource[@"data"]))
                return invalid(@"工程资源类型或内容无效。");
            NSDictionary *asset=resource[@"data"];
            id encoded=asset[@"pcm"] ?: asset[@"smf"];
            if(encoded&&![encoded isKindOfClass:NSString.class])return invalid(@"工程资源编码无效。");
            bytes+=[encoded length];
            if(decodeResources) {
                if([kind isEqual:@"audio"]) {
                    std::shared_ptr<AudioSample> sample;
                    if(!triassets::parseAudioAsset(asset,sample,why))return false;
                } else {
                    MidiClip clip;NoteModel model;
                    if(!triassets::decodeAssetMap(@{kind:asset},clip,model,why,true))return false;
                }
            }
        }
        if(bytes>size_t(512)*1024*1024)return invalid(@"单个节点的资源集合超过 512 MiB。");
    }
    if(collections&&(![collections isKindOfClass:NSDictionary.class]||[collections count]>128))
        return invalid(@"工程资源记录集合格式无效。");
    for(id key in collections) {
        id entries=collections[key];
        if(!identifier(key)||![entries isKindOfClass:NSArray.class]||[entries count]>1024)
            return invalid(@"工程资源记录格式无效或数量过多。");
        NSMutableSet *ids=[NSMutableSet new];
        for(id entry in entries) {
            if(!triassets::named(entry)||!identifier(entry[@"id"])||[ids containsObject:entry[@"id"]]||
               ![entry[@"resources"] isKindOfClass:NSDictionary.class]||
               ![entry[@"metadata"] isKindOfClass:NSDictionary.class])return invalid(@"工程资源记录格式无效。");
            [ids addObject:entry[@"id"]];
            for(id role in entry[@"resources"])
                if(!identifier(role)||!identifier(entry[@"resources"][role])||!resources[entry[@"resources"][role]])
                    return invalid(@"工程资源记录指向不存在的资源。");
        }
    }
    return true;
}
}
