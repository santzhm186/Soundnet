#pragma once
#include "NodeRegistry.hpp"
#include "AudioSample.hpp"
#include <memory>
#include <string>
#include <map>
#include <set>

namespace tri {
using InstanceID=uint64_t;
struct GraphNode {
    InstanceID id=0;
    int type=0;
    float x=0,y=0;
    std::map<int,float> parameters;
    MidiClip clip;
    NoteModel model;
    std::shared_ptr<const AudioSample> sample;
    bool bypass=false;
};
struct GraphEdge { InstanceID source=0,target=0; int port=0,sourcePort=0; };
struct GraphSpec { std::vector<GraphNode> nodes; std::vector<GraphEdge> edges; };
// These IDs are stored on graph instances, outside the AU's global parameter list.
static_assert(int(ParamCount) <= int(ArrHighMin));
enum class ArrangerTexture { Highest, Middle, Lowest, Chord };
inline constexpr bool arrangerParam(int p) { return p>=ArrHighMin && p<=ArrLowTexture; }
inline constexpr bool validGraphParam(int type,int p) {
    if(type==ArrangerNode) return arrangerParam(p);
    return nodeAcceptsParam(type,p);
}
inline const ParamInfo& graphParamInfo(int p) {
    static constexpr ParamInfo voiceParams[] = {
        {"High Minimum",0,127,72,"MIDI"}, {"High Maximum",0,127,95,"MIDI"}, {"High Texture",0,3,0,""},
        {"Middle Minimum",0,127,55,"MIDI"}, {"Middle Maximum",0,127,71,"MIDI"}, {"Middle Texture",0,3,1,""},
        {"Low Minimum",0,127,36,"MIDI"}, {"Low Maximum",0,127,54,"MIDI"}, {"Low Texture",0,3,2,""}
    };
    if(arrangerParam(p)) return voiceParams[p-ArrHighMin];
    if(const ParamInfo* info=nodeParamInfo(p)) return *info;
    return params[p];
}
inline float graphParamValue(const GraphNode& node,int p) {
    auto it=node.parameters.find(p);
    return it==node.parameters.end() ? graphParamInfo(p).def : it->second;
}
inline bool validArrangerRanges(const GraphNode& node) {
    for(int low : {ArrHighMin,ArrMidMin,ArrLowMin})
        if(graphParamValue(node,low)>graphParamValue(node,low+1)) return false;
    return true;
}
enum class Signal { None, Notes, Audio, Clock, Trigger };
inline Signal outputType(int type,int port=0) {
    if(type==ArrangerNode && port>=0 && port<3)return Signal::Notes;
    if(port==1 && type==ClipNode)return Signal::Trigger;
    if(port)return Signal::None;
    if(type>=StartNode && type<=CountNode)return Signal::Trigger;
    if(type==RandomNode)return Signal::Trigger;
    if(type==MetronomeNode)return Signal::Audio;
    if(type==TransposeNode)return Signal::Notes;
    if(type==EchoNode)return Signal::Notes;
    if(type==ChannelFilterNode)return Signal::Notes;
    if(type==GateNode)return Signal::Audio;
    if (type==QuantizeNode || noteSource(type)) return Signal::Notes;
    if(type==ClockNode || type==DawClockNode) return Signal::Clock;
    if(type==Osc1Node || type==InputNode || type==DawAudioNode || type==MixNode || type==EQNode || type==DelayNode ||
       type==FilterNode || type==ReverbNode || type==NoiseNode || type==SamplerNode ||
       type==AudioClipNode || type==CompressorNode || type==LimiterNode || type==ChorusNode ||
       type==TremoloNode || type==BitcrusherNode || type==PhaserNode) return Signal::Audio;
    return Signal::None;
}
inline int outputCount(int type) {
    if(type==ArrangerNode) return 3;
    if(type==ClipNode) return 2;
    return outputType(type)==Signal::None ? 0 : 1;
}
inline std::vector<Signal> inputTypes(int type) {
    if(type==ArpNode) return {Signal::Notes,Signal::Clock,Signal::Trigger};
    if(type==VelocityNode)return {Signal::Notes,Signal::Clock};
    if(type==ClipNode || type==ProgressNode) return {Signal::Clock,Signal::Trigger};
    if(type>=StartNode && type<=CountNode)return {Signal::Trigger};
    if(type==RandomNode)return {Signal::Trigger};
    if(type==MetronomeNode)return {Signal::Trigger};
    if(type==TransposeNode)return {Signal::Notes};
    if(type==EchoNode)return {Signal::Notes};
    if(type==ChannelFilterNode)return {Signal::Notes};
    if(type==GateNode)return {Signal::Audio};
    if(type==ChordNode || type==ModelNode || type==MidiOutNode || type==Osc1Node || type==ArrangerNode || type==SamplerNode || type==QuantizeNode) return {Signal::Notes};
    if(type==AudioClipNode)return {Signal::Trigger};
    if(type==CompressorNode)return {Signal::Audio,Signal::Audio};
    if(type==LimiterNode || type==ChorusNode || type==TremoloNode || type==BitcrusherNode || type==PhaserNode)return {Signal::Audio};
    if(type==MixNode) return {Signal::Audio,Signal::Audio,Signal::Audio,Signal::Audio};
    if(type==EQNode || type==DelayNode || type==FilterNode || type==ReverbNode ||
       type==CompressorNode || type==LimiterNode || type==ChorusNode || type==OutputNode || type==HardwareOutputNode || type==DawOutputNode) return {Signal::Audio};
    return {};
}
inline bool graphFailure(std::string* why,const char* message) {
    if(why)*why=message;
    return false;
}

inline bool graphOrder(const GraphSpec& graph,std::vector<size_t>& order,std::string* why=nullptr) {
    if(why)why->clear();
    std::map<InstanceID,size_t> index;
    for(size_t i=0;i<graph.nodes.size();++i) {
        const auto& n=graph.nodes[i];
        if(!n.id || !index.emplace(n.id,i).second)return graphFailure(why,"节点 ID 必须非零且唯一。");
        if(n.type<1 || n.type>ChannelFilterNode || n.type==Osc2Node || n.type==Osc3Node)return graphFailure(why,"节点类型无效。");
        for(auto [p,v]:n.parameters) {
            if(!validGraphParam(n.type,p))return graphFailure(why,"节点参数不属于该节点类型。");
            const auto& info=graphParamInfo(p);
            if(!std::isfinite(v) || v<info.min || v>info.max || (arrangerParam(p) && std::trunc(v)!=v))return graphFailure(why,"节点参数超出范围。");
        }
        if(n.type==ArrangerNode && !validArrangerRanges(n))return graphFailure(why,"编排器音域无效：低声部不能高于高声部。");
    }
    std::vector<std::vector<size_t>> next(graph.nodes.size());
    std::vector<size_t> pending(graph.nodes.size());
    std::set<std::pair<InstanceID,int>> occupied;
    for(auto e:graph.edges) {
        auto a=index.find(e.source),b=index.find(e.target);
        if(a==index.end() || b==index.end())return graphFailure(why,"连接引用了不存在的节点。");
        if(a==b)return graphFailure(why,"节点不能连接到自身。");
        auto ports=inputTypes(graph.nodes[b->second].type);
        if(e.port>=0 && size_t(e.port)<ports.size() && ports[e.port]!=Signal::Trigger && !occupied.emplace(e.target,e.port).second)
            return graphFailure(why,"同一输入端口只能有一条非触发连接。");
        if(e.port<0 || size_t(e.port)>=ports.size() || ports[e.port]!=outputType(graph.nodes[a->second].type,e.sourcePort))
            return graphFailure(why,"连接两端的信号类型不匹配。");
        // Feedback enters a stateful player or a wait queue on the next render
        // quantum. Audio and note dependencies remain acyclic.
        int destination=graph.nodes[b->second].type;
        bool temporal=ports[e.port]==Signal::Trigger && (destination==ClipNode || destination==ProgressNode || destination==ArpNode || destination==VelocityNode || destination==WaitNode);
        if(!temporal){++pending[b->second];next[a->second].push_back(b->second);}
    }
    order.clear();
    for(size_t i=0;i<pending.size();++i) if(!pending[i]) order.push_back(i);
    for(size_t j=0;j<order.size();++j) for(size_t dest:next[order[j]]) if(--pending[dest]==0)order.push_back(dest);
    if(order.size()!=graph.nodes.size())return graphFailure(why,"图中存在回路。");
    return true;
}
} // namespace tri
