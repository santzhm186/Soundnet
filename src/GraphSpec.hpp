#pragma once
#include "LegacyEngine.hpp"
#include <string>
#include <map>
#include <set>

namespace tri {
using InstanceID=uint64_t;
inline constexpr int HardwareOutputNode=19, DawOutputNode=20, DawClockNode=21, DawAudioNode=22;
struct GraphNode {
    InstanceID id=0;
    int type=0;
    float x=0,y=0;
    std::map<int,float> parameters;
    MidiClip clip;
    NoteModel model;
};
struct GraphEdge { InstanceID source=0,target=0; int port=0,sourcePort=0; };
struct GraphSpec { std::vector<GraphNode> nodes; std::vector<GraphEdge> edges; };
inline constexpr int StartNode=23, StopNode=24, WaitNode=25, CountNode=26;
enum class Signal { None, Notes, Audio, Clock, Trigger };
inline Signal outputType(int type,int port=0) {
    if(port==1 && type==ClipNode)return Signal::Trigger;
    if(port)return Signal::None;
    if(type>=StartNode && type<=CountNode)return Signal::Trigger;
    if (noteSource(type)) return Signal::Notes;
    if(type==ClockNode || type==DawClockNode) return Signal::Clock;
    if(type==Osc1Node || type==InputNode || type==DawAudioNode || type==MixNode || type==EQNode || type==DelayNode) return Signal::Audio;
    return Signal::None;
}
inline std::vector<Signal> inputTypes(int type) {
    if(type==ArpNode) return {Signal::Notes,Signal::Clock,Signal::Trigger};
    if(type==VelocityNode)return {Signal::Notes,Signal::Clock};
    if(type==ClipNode || type==ProgressNode) return {Signal::Clock,Signal::Trigger};
    if(type>=StartNode && type<=CountNode)return {Signal::Trigger};
    if(type==ChordNode || type==ModelNode || type==MidiOutNode || type==Osc1Node) return {Signal::Notes};
    if(type==MixNode) return {Signal::Audio,Signal::Audio,Signal::Audio,Signal::Audio};
    if(type==EQNode || type==DelayNode || type==OutputNode || type==HardwareOutputNode || type==DawOutputNode) return {Signal::Audio};
    return {};
}
inline bool graphOrder(const GraphSpec& graph,std::vector<size_t>& order) {
    std::map<InstanceID,size_t> index;
    for(size_t i=0;i<graph.nodes.size();++i) {
        const auto& n=graph.nodes[i];
        if(!n.id || !index.emplace(n.id,i).second || n.type<1 || n.type>26 || n.type==Osc2Node || n.type==Osc3Node) return false;
        for(auto [p,v]:n.parameters) if(p<0 || p>=ParamCount || routeParam(p) || !std::isfinite(v) || v<params[p].min || v>params[p].max) return false;
    }
    std::vector<std::vector<size_t>> next(graph.nodes.size());
    std::vector<size_t> pending(graph.nodes.size());
    std::set<std::pair<InstanceID,int>> occupied;
    for(auto e:graph.edges) {
        auto a=index.find(e.source),b=index.find(e.target);
        if(a==index.end() || b==index.end() || a==b) return false;
        auto ports=inputTypes(graph.nodes[b->second].type);
        if(e.port>=0 && size_t(e.port)<ports.size() && ports[e.port]!=Signal::Trigger && !occupied.emplace(e.target,e.port).second)return false;
        if(e.port<0 || size_t(e.port)>=ports.size() || ports[e.port]!=outputType(graph.nodes[a->second].type,e.sourcePort))return false;
        // Feedback enters a stateful player or a wait queue on the next render
        // quantum. Audio and note dependencies remain acyclic.
        int destination=graph.nodes[b->second].type;
        bool temporal=ports[e.port]==Signal::Trigger && (destination==ClipNode || destination==ProgressNode || destination==ArpNode || destination==VelocityNode || destination==WaitNode);
        if(!temporal){++pending[b->second];next[a->second].push_back(b->second);}
    }
    order.clear();
    for(size_t i=0;i<pending.size();++i) if(!pending[i]) order.push_back(i);
    for(size_t j=0;j<order.size();++j) for(size_t dest:next[order[j]]) if(--pending[dest]==0)order.push_back(dest);
    return order.size()==graph.nodes.size();
}
} // namespace tri
