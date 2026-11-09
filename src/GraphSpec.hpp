#pragma once
// Graph document model and its compiler.
//
// A GraphSpec is the editable document: stable identity, stable port IDs,
// parameters by stable ID. compileGraph() turns it into a CompiledGraph — a run
// plan with resolved port indices, adjacency and diagnostics. Validation rules
// come exclusively from NodeSpec; there is no type ceiling, no "port 0 is the
// main input" convention and no second copy of the port tables.
#include "NodeRegistry.hpp"
#include "AudioSample.hpp"
#include <memory>
#include <string>
#include <map>
#include <set>
#include <vector>

namespace tri {
using InstanceID=uint64_t;

struct GraphNode {
    InstanceID id=0;
    int type=0;                 // kernel selector, resolved from `key`
    std::string key;            // canonical identity, e.g. "soundnet.oscillator"
    uint16_t nodeVersion=1;
    float x=0,y=0;
    std::map<int,float> parameters;
    // Text parameters (prompt, lyrics, backend path). Declared by
    // NodeSpec::texts; persisted in the v4 `texts` dictionary. Kept apart from
    // numeric parameters because they have no value range and no numeric ID.
    std::map<std::string,std::string> texts;
    MidiClip clip;
    NoteModel model;
    std::shared_ptr<const AudioSample> sample;
    bool bypass=false;
    // Unknown/unavailable nodes stay in the document so a project written by a
    // newer build still round-trips. They are excluded from execution.
    bool unavailable=false;
    std::string unavailableReason;
};

struct GraphEdge {
    InstanceID source=0,target=0;
    int port=0,sourcePort=0;             // resolved indices for the engine
    std::string portId,sourcePortId;     // persisted stable IDs
};

struct GraphSpec {
    std::vector<GraphNode> nodes;
    std::vector<GraphEdge> edges;
    std::string projectId;
};

static_assert(int(ParamCount) <= int(ArrHighMin));

inline const ParamInfo& graphParamInfo(int id) {
    const ParamSpec* spec=paramSpec(id);
    static const ParamInfo unknown{"?",0,0,0,""};
    return spec?*spec->info:unknown;
}
inline float graphParamValue(const GraphNode& node,int id) {
    auto it=node.parameters.find(id);
    return it==node.parameters.end() ? graphParamInfo(id).def : it->second;
}
inline bool validGraphParam(int type,int id) {return nodeAcceptsParam(type,id);}
inline bool validArrangerRanges(const GraphNode& node) {
    for(int low : {ArrHighMin,ArrMidMin,ArrLowMin})
        if(graphParamValue(node,low)>graphParamValue(node,low+1)) return false;
    return true;
}
inline bool validAudioRanges(const GraphNode& node) {
    if(node.type==AudioClipNode)return graphParamValue(node,AudioClipStart)<graphParamValue(node,AudioClipEnd);
    if(node.type==SamplerNode)return graphParamValue(node,SamplerStart)<graphParamValue(node,SamplerEnd);
    return true;
}

inline constexpr size_t maxGraphNodes=4096;
inline constexpr size_t maxGraphEdges=16384;

// ---------------------------------------------------------------------------
// Compiled run plan
// ---------------------------------------------------------------------------
struct PortLink {
    int node=-1;   // index into GraphSpec::nodes / CompiledGraph::nodes
    int port=-1;   // source output port index
    bool connected() const {return node>=0;}
};

struct PlanNode {
    int type=0;
    const NodeSpec* spec=nullptr;
    std::vector<PortLink> inputs;                    // one entry per input port
    std::vector<std::vector<int>> triggerInputs;     // upstream nodes per trigger port
    bool bypass=false;
};

struct Diagnostic {
    InstanceID node=0;
    std::string message;
    bool fatal=false;
};

struct CompiledGraph {
    std::vector<PlanNode> nodes;   // aligned with GraphSpec::nodes
    std::vector<size_t> order;     // topological order of index positions
    std::vector<Diagnostic> diagnostics;
    bool ok=true;

    bool fatal() const {
        for(const auto& d:diagnostics) if(d.fatal) return true;
        return !ok;
    }
    // Build the reason string used by validators and the editor status line.
    std::string reason() const;
};

namespace detail {
inline bool graphFailure(std::string* why,const char* message) {
    if(why)*why=message;
    return false;
}
}

// Resolve keys into types, types into keys, and edges into their stable IDs.
// A present ID always wins over an index; a missing ID is derived from the
// index. This is the single place where legacy v3 documents become v4-shaped
// in memory, and where v4 documents become executable indices.
inline bool normalizeGraph(GraphSpec& graph,std::string* why=nullptr) {
    if(why)why->clear();
    const bool strict=true;
    for(auto& node:graph.nodes) {
        if(node.type==0 && !node.key.empty()) node.type=nodeTypeForKey(node.key.c_str());
        if(node.key.empty()) node.key=nodeKey(node.type);
        if(node.type!=0 && node.key.empty()) return detail::graphFailure(why,"节点类型没有稳定标识。");
    }
    // One index for the whole edge list: a scan per edge would be O(E*N), which
    // matters at the 4096/16384 project budget.
    std::map<InstanceID,const GraphNode*> byId;
    for(const auto& node:graph.nodes) byId.emplace(node.id,&node);
    for(auto& edge:graph.edges) {
        auto foundSource=byId.find(edge.source),foundTarget=byId.find(edge.target);
        const GraphNode* source=foundSource==byId.end()?nullptr:foundSource->second;
        const GraphNode* target=foundTarget==byId.end()?nullptr:foundTarget->second;
        if(!source||!target)continue;   // reported by compileGraph
        // Edges that touch an unavailable node are carried through untouched so
        // a newer project can be saved again without losing them.
        if(source->unavailable||target->unavailable||!source->type||!target->type)continue;
        if(!edge.sourcePortId.empty()) {
            const int index=nodePortIndex(source->type,false,edge.sourcePortId.c_str());
            if(index<0) {
                if(strict) return detail::graphFailure(why,"连线引用了不存在的输出端口名称。");
            } else edge.sourcePort=index;
        } else {
            edge.sourcePortId=nodePortId(source->type,false,size_t(edge.sourcePort));
        }
        if(!edge.portId.empty()) {
            const int index=nodePortIndex(target->type,true,edge.portId.c_str());
            if(index<0) {
                if(strict) return detail::graphFailure(why,"连线引用了不存在的输入端口名称。");
            } else edge.port=index;
        } else {
            edge.portId=nodePortId(target->type,true,size_t(edge.port));
        }
    }
    return true;
}

// Compile the document into a run plan. `allowUnknownNodes` keeps unavailable
// node types in the document (the editor and project loader use it); strict
// callers such as the offline exporter reject them.
inline bool compileGraph(const GraphSpec& source,CompiledGraph& plan,std::string* why=nullptr,
                         bool allowUnknownNodes=false) {
    if(why)why->clear();
    plan=CompiledGraph{};
    auto fail=[&](const char* message){
        plan.ok=false;
        plan.diagnostics.push_back({0,message,true});
        if(why)*why=message;
        return false;
    };
    if(source.nodes.size()>maxGraphNodes)return fail("节点数量超过上限。");
    if(source.edges.size()>maxGraphEdges)return fail("连线数量超过上限。");

    GraphSpec graph=source;
    std::string normalizeWhy;
    if(!normalizeGraph(graph,&normalizeWhy)) {
        plan.ok=false;
        plan.diagnostics.push_back({0,normalizeWhy,true});
        if(why)*why=normalizeWhy;
        return false;
    }

    std::map<InstanceID,size_t> index;
    plan.nodes.resize(graph.nodes.size());
    for(size_t i=0;i<graph.nodes.size();++i) {
        const auto& node=graph.nodes[i];
        if(!node.id)return fail("节点 ID 必须非零。");
        if(!index.emplace(node.id,i).second)return fail("节点 ID 必须唯一。");
        auto& entry=plan.nodes[i];
        entry.spec=nodeSpec(node.type);
        if(!entry.spec) {
            entry.type=0;
            const std::string message="未知节点类型："+(node.key.empty()?std::to_string(node.type):node.key);
            plan.diagnostics.push_back({node.id,message,!allowUnknownNodes});
            if(!allowUnknownNodes) {plan.ok=false;if(why)*why=message;return false;}
            continue;
        }
        if(!entry.spec->instantiable) {
            const std::string message=std::string("节点类型不可实例化：")+entry.spec->key;
            plan.diagnostics.push_back({node.id,message,true});
            plan.ok=false;if(why)*why=message;
            return false;
        }
        entry.type=node.type;
        entry.bypass=node.bypass && nodeHas(entry.spec->capabilities,CapBypassable);
        entry.inputs.assign(entry.spec->inputs.size(),PortLink{});
        entry.triggerInputs.resize(entry.spec->inputs.size());
        if(node.bypass && !nodeHas(entry.spec->capabilities,CapBypassable)) {
            plan.diagnostics.push_back({node.id,std::string("节点不支持旁通：")+entry.spec->title,false});
        }
        for(const auto& [id,value]:node.parameters) {
            const ParamSpec* param=nodeParamSpec(node.type,id);
            if(!param) {
                // A parameter this build knows but this node does not declare is a
                // document error. A parameter this build has never heard of is
                // preserved for a newer version instead of rejecting the project.
                if(paramSpec(id)) {
                    const std::string message=std::string("参数不属于该节点：")+paramKey(id);
                    plan.diagnostics.push_back({node.id,message,true});
                    plan.ok=false;if(why)*why=message;
                    return false;
                }
                plan.diagnostics.push_back({node.id,std::string("保留未知参数：")+std::to_string(id),false});
                continue;
            }
            const ParamInfo& info=*param->info;
            if(!std::isfinite(value)||value<info.min||value>info.max) {
                const std::string message=std::string("参数超出范围：")+param->key;
                plan.diagnostics.push_back({node.id,message,true});
                plan.ok=false;if(why)*why=message;
                return false;
            }
            if(param->discrete && std::trunc(value)!=value) {
                const std::string message=std::string("参数必须为整数：")+param->key;
                plan.diagnostics.push_back({node.id,message,true});
                plan.ok=false;if(why)*why=message;
                return false;
            }
        }
        if(node.type==ArrangerNode && !validArrangerRanges(node))return fail("编排器音域无效：低声部不能高于高声部。");
        if(node.type==AudioClipNode && !validAudioRanges(node))return fail("音频片段起止位置无效：开始必须早于结束。");
        if(node.type==SamplerNode && !validAudioRanges(node))return fail("采样器起止位置无效：开始必须早于结束。");
        // Asset presence is not checked here: the compiler also runs on documents
        // whose assets were deliberately not decoded. The loader reports it.
    }

    // Edges.
    std::set<std::pair<InstanceID,int>> occupied;
    std::vector<std::vector<size_t>> next(graph.nodes.size());
    std::vector<size_t> pending(graph.nodes.size(),0);
    for(auto edge:graph.edges) {
        auto a=index.find(edge.source),b=index.find(edge.target);
        if(a==index.end()||b==index.end())return fail("连接引用了不存在的节点。");
        auto& targetPlan=plan.nodes[b->second];
        const auto& targetNode=graph.nodes[b->second];
        const auto& sourceNode=graph.nodes[a->second];
        if(!targetPlan.spec) {
            plan.diagnostics.push_back({targetNode.id,"连接指向缺失节点，已忽略。",false});
            continue;
        }
        if(a==b)return fail("节点不能连接到自身。");
        if(edge.port<0||size_t(edge.port)>=targetPlan.spec->inputs.size())
            return fail("连线指向不存在的输入端口。");
        if(!plan.nodes[a->second].spec) {
            plan.diagnostics.push_back({sourceNode.id,"连接来自缺失节点，已忽略。",false});
            continue;
        }
        const PortSpec& inputPort=targetPlan.spec->inputs[size_t(edge.port)];
        if(edge.sourcePort<0||size_t(edge.sourcePort)>=plan.nodes[a->second].spec->outputs.size())
            return fail("连线引用了不存在的输出端口。");
        const Signal produced=plan.nodes[a->second].spec->outputs[size_t(edge.sourcePort)].signal;
        if(produced!=inputPort.signal)return fail("连接两端的信号类型不匹配。");
        const bool trigger=inputPort.role==RoleTrigger||inputPort.signal==Signal::Trigger;
        // Ordinary inputs are single-source: the run plan stores one upstream
        // link per port, so accepting a second edge would silently drop one.
        // Trigger inputs accept several distinct sources; identical duplicates
        // are dropped below.
        if(!trigger && !occupied.emplace(edge.target,edge.port).second)
            return fail("同一输入端口只能有一条连接。");
        auto& links=targetPlan.triggerInputs[size_t(edge.port)];
        if(trigger) {
            // Identical duplicates used to double-trigger downstream nodes.
            bool duplicate=false;
            for(int existing:links) if(existing==int(a->second))duplicate=true;
            if(duplicate) {
                plan.diagnostics.push_back({targetNode.id,"忽略重复的触发连线。",false});
                continue;
            }
            links.push_back(int(a->second));
        } else {
            targetPlan.inputs[size_t(edge.port)]=PortLink{int(a->second),edge.sourcePort};
        }
        // Feedback boundaries (clip/arp/wait style inputs) are excluded from the
        // ordering constraint; the engine defers those events by one block.
        if(!(inputPort.flags&PortFeedback)) {
            ++pending[b->second];
            next[a->second].push_back(b->second);
        }
    }
    plan.order.clear();
    for(size_t i=0;i<pending.size();++i) if(!pending[i]) plan.order.push_back(i);
    for(size_t j=0;j<plan.order.size();++j)
        for(size_t dest:next[plan.order[j]]) if(--pending[dest]==0) plan.order.push_back(dest);
    if(plan.order.size()!=graph.nodes.size())return fail("图中存在回路。");
    for(const auto& node:graph.nodes)
        if(node.unavailable) plan.diagnostics.push_back({node.id,node.unavailableReason,false});
    return plan.ok;
}

inline std::string CompiledGraph::reason() const {
    for(const auto& d:diagnostics) if(d.fatal) return d.message;
    for(const auto& d:diagnostics) if(!d.message.empty()) return d.message;
    return {};
}

// Legacy entry point kept for tests and callers that only need the order.
inline bool graphOrder(const GraphSpec& graph,std::vector<size_t>& order,std::string* why=nullptr) {
    CompiledGraph plan;
    if(!compileGraph(graph,plan,why)) {order.clear();return false;}
    order=plan.order;
    return true;
}
} // namespace tri
