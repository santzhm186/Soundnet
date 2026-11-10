#pragma once
// Compatibility façade over NodeSpec.
//
// The registry used to be the source of truth (six parallel tables in this file
// alone, plus port tables in GraphSpec and 67 type checks in InstanceGraph).
// It is now only a view: every function below answers from `NodeSpec`, so the
// editor, the validator, the engine and the tests can never disagree about what
// a node is.
#include "NodeSpec.hpp"

namespace tri {
struct NodeMeta {
    int type;
    const char* title;
    const char* category;
    bool visibleInCatalog;
    bool bypassable;
};

inline const NodeMeta* nodeMeta(int type) {
    static thread_local std::map<int,NodeMeta> table;
    static thread_local uint64_t generation=UINT64_MAX;
    auto snapshot=nodeCatalogSnapshot();
    if(generation!=snapshot->generation) {
        table.clear();for(const auto& spec:snapshot->specs)if(spec.instantiable)
            table.emplace(spec.type,NodeMeta{spec.type,spec.title,spec.category,spec.visible,nodeHas(spec.capabilities,CapBypassable)});
        generation=snapshot->generation;
    }
    auto found=table.find(type);return found==table.end()?nullptr:&found->second;
}

inline const char* nodeTitle(int type) {
    const NodeSpec* spec=nodeSpec(type);
    return spec?spec->title:"节点";
}
inline const char* nodeCategory(int type) {
    const NodeSpec* spec=nodeSpec(type);
    return spec?spec->category:"";
}

// Palette order: registration order, filtered to visible instantiable nodes.
inline const std::vector<int>& nodeCatalog() {
    static thread_local std::vector<int> catalog;
    static thread_local uint64_t generation=UINT64_MAX;
    auto snapshot=nodeCatalogSnapshot();
    if(generation!=snapshot->generation) {
        catalog.clear();for(const auto& spec:snapshot->specs)if(spec.visible&&spec.instantiable)catalog.push_back(spec.type);
        generation=snapshot->generation;
    }
    return catalog;
}

// Global parameter metadata by stable ID (legacy AU parameters and graph ones).
inline const ParamInfo* nodeParamInfo(int id) {
    const ParamSpec* spec=paramSpec(id);
    return spec?spec->info:nullptr;
}
inline bool discreteParam(int id) {
    const ParamSpec* spec=paramSpec(id);
    return spec && spec->discrete;
}

// Declared parameters of one node type, in display order.
inline std::vector<int> nodeParamList(int type) {
    const NodeSpec* spec=nodeSpec(type);
    return spec?spec->params:std::vector<int>{};
}

// ---- port views -----------------------------------------------------------
inline Signal outputType(int type,int port=0) {
    const PortSpec* spec=nodePort(type,false,size_t(port));
    return spec?spec->signal:Signal::None;
}
inline int outputCount(int type) {return nodeOutputCount(type);}
inline std::vector<Signal> inputTypes(int type) {
    std::vector<Signal> signals;
    const NodeSpec* spec=nodeSpec(type);
    if(!spec)return signals;
    signals.reserve(spec->inputs.size());
    for(const auto& port:spec->inputs) signals.push_back(port.signal);
    return signals;
}
inline std::vector<Signal> outputTypes(int type) {
    std::vector<Signal> signals;
    const NodeSpec* spec=nodeSpec(type);
    if(!spec)return signals;
    signals.reserve(spec->outputs.size());
    for(const auto& port:spec->outputs) signals.push_back(port.signal);
    return signals;
}
// Node types that can feed a note/MIDI input.
inline std::vector<int> noteSourceTypes() {
    const std::vector<int> types = []{
        std::vector<int> v;
        for(const auto& spec:nodeSpecs())
            if(spec.instantiable && nodeHas(spec.capabilities,CapNoteSource))
                for(const auto& port:spec.outputs)
                    if(port.signal==Signal::Midi) {v.push_back(spec.type);break;}
        return v;
    }();
    return types;
}
} // namespace tri
