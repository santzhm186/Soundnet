#pragma once
#include "NodeDescriptor.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <set>
namespace tri {
// Catalog generations are immutable. Refresh runs on the control thread; live
// plans retain a generation and its loaded code for their complete lifetime.
struct NodeCatalog {
    uint64_t generation=0;
    std::vector<NodeModule> modules;
    std::vector<NodeSpec> specs;
    std::map<int,ParamSpec> parameters;
    std::map<int,const NodeSpec*> byType;
    std::map<std::string,const NodeSpec*> byKey;
    std::map<std::string,void*> editors;
    std::vector<std::shared_ptr<void>> code;
};
inline std::shared_ptr<NodeCatalog> makeNodeCatalog(std::vector<NodeModule> modules,
    std::map<std::string,void*> editors={},std::vector<std::shared_ptr<void>> code={}) {
    auto result=std::make_shared<NodeCatalog>();
    result->modules=std::move(modules);result->editors=std::move(editors);result->code=std::move(code);
    std::set<std::string> keys;std::set<int> types;
    for(const auto& module:result->modules) {
        const auto& spec=module.spec;
        if(!spec.key||!std::strchr(spec.key,'.')||!keys.insert(spec.key).second)
            throw std::logic_error("Duplicate or invalid node identity");
        if(spec.type!=nodeTypeID(spec.key)||!types.insert(spec.type).second)
            throw std::logic_error("Invalid or conflicting derived node type ID");
        if(spec.sdkVersion!=nodeSDKVersion||!spec.nodeVersion||!spec.stateVersion||spec.legacyAdapter)
            throw std::logic_error("Unsupported node contract: "+std::string(spec.key));
        if(!spec.kernelFactory&&(spec.capabilities!=0&&spec.capabilities!=CapTask))
            throw std::logic_error("Missing node kernel: "+std::string(spec.key));
        std::set<int> localParameters;
        for(const auto& parameter:module.parameters) {
            if(!parameter.second.key||!parameter.second.info||
               parameter.first!=parameterID(spec.key,parameter.second.key))
                throw std::logic_error("Invalid derived parameter ID: "+std::string(spec.key));
            registerParameter(result->parameters,parameter.first,parameter.second);
            localParameters.insert(parameter.first);
        }
        for(int parameter:spec.params)if(!localParameters.count(parameter))
            throw std::logic_error("Node parameter is not declared locally: "+std::string(spec.key));
        for(bool input:{true,false}) {
            std::set<std::string> ports;
            for(const auto& port:input?spec.inputs:spec.outputs) {
                if(!port.id||!ports.insert(port.id).second)throw std::logic_error("Conflicting node ports");
                if(port.signal!=Signal::Audio&&port.signal!=Signal::Midi&&port.signal!=Signal::Trigger&&port.signal!=Signal::Clock)
                    throw std::logic_error("Unsupported node signal");
                if((port.flags&PortMultiSource)&&(!input||port.signal!=Signal::Trigger))
                    throw std::logic_error("Unsupported multi-source port");
                if((port.flags&PortFeedback)&&(!input||port.signal!=Signal::Trigger))
                    throw std::logic_error("Feedback requires a trigger input");
            }
        }
        result->specs.push_back(spec);
    }
    for(const auto& spec:result->specs){result->byType.emplace(spec.type,&spec);result->byKey.emplace(spec.key,&spec);}
    return result;
}
const std::vector<NodeModule>& builtinNodeModules();
#ifdef SOUNDNET_DYNAMIC_NODES
std::shared_ptr<const NodeCatalog>& nodeCatalogReadOverride();
std::shared_ptr<const NodeCatalog> nodeCatalogSnapshot();
void publishNodeCatalog(std::shared_ptr<NodeCatalog> catalog);
#else
inline std::shared_ptr<const NodeCatalog>& nodeCatalogReadOverride(){static thread_local std::shared_ptr<const NodeCatalog> value;return value;}
inline std::shared_ptr<const NodeCatalog>& catalogStorage() {
    static std::shared_ptr<const NodeCatalog> current=makeNodeCatalog(builtinNodeModules());return current;
}
inline std::shared_ptr<const NodeCatalog> nodeCatalogSnapshot(){
    if(nodeCatalogReadOverride())return nodeCatalogReadOverride();
    return std::atomic_load(&catalogStorage());
}
inline void publishNodeCatalog(std::shared_ptr<NodeCatalog> catalog) {
    static std::vector<std::shared_ptr<const NodeCatalog>> retired;
    auto previous=std::atomic_load(&catalogStorage());
    if(retired.size()>=512)throw std::runtime_error("Restart required after 512 catalog revisions");
    retired.push_back(previous);catalog->generation=previous->generation+1;
    std::atomic_store(&catalogStorage(),std::shared_ptr<const NodeCatalog>(std::move(catalog)));
}
#endif
class NodeCatalogReadScope {
    std::shared_ptr<const NodeCatalog> previous;
public:
    explicit NodeCatalogReadScope(std::shared_ptr<const NodeCatalog> snapshot=nodeCatalogSnapshot()):previous(nodeCatalogReadOverride()) {nodeCatalogReadOverride()=std::move(snapshot);}
    ~NodeCatalogReadScope(){nodeCatalogReadOverride()=std::move(previous);}
    NodeCatalogReadScope(const NodeCatalogReadScope&)=delete;
    NodeCatalogReadScope& operator=(const NodeCatalogReadScope&)=delete;
};
inline const std::vector<NodeModule>& nodeModules(){return nodeCatalogSnapshot()->modules;}
inline void* loadedNodeEditor(const char* key) {
    if(!key)return nullptr;
    auto catalog=nodeCatalogSnapshot();const auto found=catalog->editors.find(key);
    return found==catalog->editors.end()?nullptr:found->second;
}
}
