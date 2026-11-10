#pragma once
// Node registry view. Definitions live in nodes/<module>/Node.hpp; the public
// API lives in platform/NodeDescriptor.hpp. Never add node-specific lists here.
#include "NodeCatalog.hpp"
#include <cmath>

namespace tri {

// The global parameter table. IDs are the persisted identity; keys are the
// readable alias used by v4 documents. Reservation: never reuse an ID or key.
inline const std::map<int,ParamSpec>& paramSpecs() {
    return nodeCatalogSnapshot()->parameters;
}
inline const ParamSpec* paramSpec(int id) {
    const auto& table=paramSpecs();
    auto it=table.find(id);
    return it==table.end()?nullptr:&it->second;
}
inline const char* paramKey(int id) {
    const ParamSpec* spec=paramSpec(id);
    return spec?spec->key:"";
}
inline int paramIdForKey(const char* key) {
    if(!key||!*key)return -1;
    for(const auto& row:paramSpecs()) if(std::strcmp(row.second.key,key)==0) return row.first;
    return -1;
}

// The module list is generated from nodes/*/Node.hpp, never edited by hand.
inline const std::vector<NodeSpec>& nodeSpecs(){return nodeCatalogSnapshot()->specs;}
inline const std::map<int,const NodeSpec*>& nodeSpecIndex(){return nodeCatalogSnapshot()->byType;}
inline const std::map<std::string,const NodeSpec*>& nodeKeyIndex(){return nodeCatalogSnapshot()->byKey;}

inline const NodeSpec* nodeSpec(int type) {
    const auto& index=nodeSpecIndex();
    auto it=index.find(type);
    return it==index.end()?nullptr:it->second;
}
inline const NodeSpec* nodeSpecForKey(const char* key) {
    if(!key||!*key)return nullptr;
    const auto& index=nodeKeyIndex();
    auto it=index.find(key);
    return it==index.end()?nullptr:it->second;
}
inline const char* nodeKey(int type) {
    const NodeSpec* spec=nodeSpec(type);
    return spec?spec->key:"";
}
inline int nodeTypeForKey(const char* key) {
    const NodeSpec* spec=nodeSpecForKey(key);
    return spec?spec->type:0;
}
// A type is valid when it is a registered, instantiable kernel. There is no
// numeric ceiling any more: type 47 registers like type 1 does.
inline bool validNodeType(int type,bool allowMigrationOnly=false) {
    const NodeSpec* spec=nodeSpec(type);
    return spec && (allowMigrationOnly || spec->instantiable);
}

// Port lookup. `port` is the descriptor position, which is also the persisted
// v3 index. Out-of-range access returns nullptr instead of reading past a table.
inline const PortSpec* nodePort(int type,bool input,size_t index) {
    const NodeSpec* spec=nodeSpec(type);
    if(!spec)return nullptr;
    const auto& ports=input?spec->inputs:spec->outputs;
    return index<ports.size()?&ports[index]:nullptr;
}
inline int nodePortIndex(int type,bool input,const char* id) {
    const NodeSpec* spec=nodeSpec(type);
    if(!spec||!id)return -1;
    const auto& ports=input?spec->inputs:spec->outputs;
    for(size_t i=0;i<ports.size();++i) if(std::strcmp(ports[i].id,id)==0) return int(i);
    return -1;
}
inline const char* nodePortId(int type,bool input,size_t index) {
    const PortSpec* port=nodePort(type,input,index);
    return port?port->id:"";
}
inline int nodeInputCount(int type) {
    const NodeSpec* spec=nodeSpec(type);
    return spec?int(spec->inputs.size()):0;
}
inline int nodeOutputCount(int type) {
    const NodeSpec* spec=nodeSpec(type);
    return spec?int(spec->outputs.size()):0;
}
// First input port with the given role, or -1. Roles replace the old habit of
// reading `inputs[0]` and guessing.
inline int nodeInputByRole(int type,PortRole role) {
    const NodeSpec* spec=nodeSpec(type);
    if(!spec)return -1;
    for(size_t i=0;i<spec->inputs.size();++i) if(spec->inputs[i].role==role) return int(i);
    return -1;
}
inline int nodeInputByRole(int type,PortRole role,Signal signal) {
    const NodeSpec* spec=nodeSpec(type);
    if(!spec)return -1;
    for(size_t i=0;i<spec->inputs.size();++i)
        if(spec->inputs[i].role==role && spec->inputs[i].signal==signal) return int(i);
    return -1;
}
inline const ParamSpec* nodeParamSpec(int type,int id) {
    const NodeSpec* spec=nodeSpec(type);
    if(!spec)return nullptr;
    for(int candidate:spec->params) if(candidate==id) return paramSpec(id);
    return nullptr;
}
inline int nodeParamIdForKey(int type,const char* key) {
    const NodeSpec* spec=nodeSpec(type);
    if(!spec||!key)return -1;
    for(int id:spec->params) {
        const ParamSpec* p=paramSpec(id);
        if(p&&std::strcmp(p->key,key)==0)return id;
    }
    return -1;
}
inline bool nodeAcceptsParam(int type,int id) {return nodeParamSpec(type,id)!=nullptr;}
// Text parameter lookup. Text specs are declared per node and carry no numeric
// identity; persistence uses the stable string key directly.
inline const TextSpec* nodeTextSpec(int type,const char* id) {
    const NodeSpec* spec=nodeSpec(type);
    if(!spec||!id)return nullptr;
    for(const auto& text:spec->texts) if(std::strcmp(text.id,id)==0) return &text;
    return nullptr;
}
inline int nodeTextCount(int type) {
    const NodeSpec* spec=nodeSpec(type);
    return spec?int(spec->texts.size()):0;
}
inline const TextSpec* nodeText(int type,size_t index) {
    const NodeSpec* spec=nodeSpec(type);
    return spec && index<spec->texts.size()?&spec->texts[index]:nullptr;
}
inline bool nodeParamDiscrete(int type,int id) {
    const ParamSpec* spec=nodeParamSpec(type,id);
    return spec && spec->discrete;
}
inline bool nodeHas(uint32_t capabilities,uint32_t flag) {return (capabilities&flag)!=0;}
inline uint32_t nodeCapabilities(int type) {
    const NodeSpec* spec=nodeSpec(type);
    return spec?spec->capabilities:0;
}

// The descriptor and graph compiler must reject the same unsupported contracts.
inline const char* nodePortContractProblem(const NodeSpec& spec,const PortSpec& port,bool input) {
    if((port.flags&PortMultiSource) && (!input || port.signal!=Signal::Trigger))
        return "多源只适用于触发输入。";
    if(spec.kernelFactory) {
        if(port.signal!=Signal::Audio && port.signal!=Signal::Midi &&
           port.signal!=Signal::Trigger && port.signal!=Signal::Clock)
            return "节点声明了当前 SDK 不支持的流端口。";
        if(port.role==RoleTrigger && port.signal!=Signal::Trigger)
            return "触发端口角色必须使用触发信号。";
        if((port.flags&PortFeedback) && (!input || port.signal!=Signal::Trigger))
            return "反馈端口必须为触发输入。";
    }
    return nullptr;
}

// Self-check used by tests and by the editor at launch. Returns an empty string
// when the registry is internally consistent.
inline std::string registryProblems() {
    NodeCatalogReadScope catalogScope;
    std::string why;
    std::map<std::string,int> keys;
    std::map<int,std::string> types;
    for(const auto& spec:nodeSpecs()) {
        std::map<std::string,int> paramKeys;
        if(!spec.key||!*spec.key) {why+="节点缺少 nodeKey。\n";continue;}
        if(!keys.emplace(spec.key,spec.type).second) why+="nodeKey 重复："+std::string(spec.key)+"\n";
        if(!types.emplace(spec.type,spec.key).second) why+="节点类型编号重复："+std::to_string(spec.type)+"\n";
        if(!spec.title||!*spec.title) why+=std::string(spec.key)+" 缺少标题。\n";
        if(!spec.category||!*spec.category) why+=std::string(spec.key)+" 缺少分类。\n";
        if(spec.kernelFactory && spec.legacyAdapter)why+=std::string(spec.key)+" 同时注册了旧适配器与新内核。\n";
        const bool executes=(spec.capabilities&(CapData|CapStream|CapGenerator|CapTriggerSource|CapTriggerTarget|
                              CapNoteSource|CapClockSource|CapOutputSink)) || !spec.inputs.empty() || !spec.outputs.empty();
        if(spec.instantiable && executes && !spec.kernelFactory && !spec.legacyAdapter)
            why+=std::string(spec.key)+" 缺少内核工厂。\n";
        if(spec.nodeVersion==0) why+=std::string(spec.key)+" 的 nodeVersion 必须为正。\n";
        if(spec.instantiable && spec.outputs.empty() && spec.inputs.empty() &&
           spec.capabilities!=0 && spec.capabilities!=CapTask && spec.capabilities!=0 && spec.environmentRole==EnvironmentRole::None)
            why+=std::string(spec.key)+" 声明了能力却没有端口。\n";
        if(spec.instantiable && nodeHas(spec.capabilities,CapOutputSink) &&
           spec.inputs.empty()) why+=std::string(spec.key)+" 是输出汇聚节点却没有输入。\n";
        for(bool input:{true,false}) {
            std::map<std::string,int> ids;
            const auto& ports=input?spec.inputs:spec.outputs;
            for(const auto& port:ports) {
                if(!port.id||!*port.id) why+=std::string(spec.key)+" 存在空端口 ID。\n";
                if(!ids.emplace(port.id,1).second) why+=std::string(spec.key)+" 同方向端口 ID 重复："+port.id+"\n";
                if(const char* problem=nodePortContractProblem(spec,port,input))
                    why+=std::string(spec.key)+" "+problem+"\n";
                if(port.signal==Signal::None) why+=std::string(spec.key)+" 端口 "+port.id+" 的信号为 None。\n";
            }
        }
        if(spec.instantiable) {
            if(spec.outputs.empty() && !nodeHas(spec.capabilities,CapOutputSink) &&
               spec.capabilities!=0 && spec.capabilities!=CapTask && spec.environmentRole==EnvironmentRole::None) why+=std::string(spec.key)+" 没有输出端口。\n";
            const bool source=nodeHas(spec.capabilities,CapGenerator)||nodeHas(spec.capabilities,CapNoteSource)||
                              nodeHas(spec.capabilities,CapClockSource)||nodeHas(spec.capabilities,CapStream);
            // Comments are deliberately inert: no ports, no capabilities.

            if(spec.capabilities!=0 && spec.capabilities!=CapTask && spec.environmentRole==EnvironmentRole::None && spec.inputs.empty() && !source)
                why+=std::string(spec.key)+" 既无输入也无生成能力。\n";
        }
        std::map<int,int> paramIds;
        for(int id:spec.params) {
            const ParamSpec* param=paramSpec(id);
            if(!param) {why+=std::string(spec.key)+" 引用了不存在的参数 "+std::to_string(id)+"。\n";continue;}
            if(!paramIds.emplace(id,1).second) why+=std::string(spec.key)+" 参数重复："+param->key+"\n";
            if(!param->key||!*param->key) why+=std::string(spec.key)+" 的参数缺少稳定键。\n";
            auto inserted=paramKeys.emplace(param->key,id);
            if(!inserted.second && inserted.first->second!=id)
                why+="参数键重复："+std::string(param->key)+"（"+std::to_string(inserted.first->second)+
                     " 与 "+std::to_string(id)+"）\n";
            const ParamInfo& info=*param->info;
            if(!(info.min<=info.def && info.def<=info.max)) why+=std::string(param->key)+" 的默认值越界。\n";
            if(param->discrete && (info.min!=std::trunc(info.min)||info.max!=std::trunc(info.max)||
                                   info.def!=std::trunc(info.def)))
                why+=std::string(param->key)+" 标记为离散但范围含小数。\n";
            if(param->options) {
                const double span=double(info.max)-double(info.min)+1.0;
                if(param->optionCount==0||span!=double(param->optionCount))
                    why+=std::string(param->key)+" 的枚举标签数量与取值范围不一致。\n";
                if(info.min!=0)
                    why+=std::string(param->key)+" 的枚举必须从 0 开始。\n";
            }
        }
        if(spec.assetKind) {
            const bool declared=nodeHas(spec.capabilities,CapAssetClip)||nodeHas(spec.capabilities,CapAssetAudio)||
                                nodeHas(spec.capabilities,CapAssetModel);
            if(!declared) why+=std::string(spec.key)+" 有素材类型但未声明素材能力。\n";
        }
        std::map<std::string,int> textIds;
        for(const auto& text:spec.texts) {
            if(!text.id||!*text.id) why+=std::string(spec.key)+" 存在空文本参数 ID。\n";
            if(!text.label||!*text.label) why+=std::string(spec.key)+" 的文本参数缺少标签："+(text.id?text.id:"")+"\n";
            if(!textIds.emplace(text.id?text.id:"",1).second)
                why+=std::string(spec.key)+" 的文本参数 ID 重复："+(text.id?text.id:"")+"\n";
            if(text.defaultValue && std::strlen(text.defaultValue)>text.maxBytes)
                why+=std::string(spec.key)+" 的文本默认值超过长度预算。\n";
        }
    }
    return why;
}
} // namespace tri

#ifndef SOUNDNET_DYNAMIC_NODES
#include "../nodes/GeneratedRegistry.hpp"
#endif
