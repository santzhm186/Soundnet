// The node contract: descriptors are the single source of truth, ports have
// stable names, parameter scope is exact and the compiler derives every rule
// from NodeSpec. These tests fail if a future node is added inconsistently or
// if a rule drifts back into a hardcoded list.
#include "../src/GraphSpec.hpp"
#include <cassert>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <set>
#include <string>
using namespace tri;

static GraphNode node(uint64_t id,int type) {
    GraphNode n;
    n.id=id;n.type=type;n.key=nodeKey(type);n.nodeVersion=nodeSpec(type)->nodeVersion;
    return n;
}
static void connect(GraphSpec& graph,uint64_t source,const char* sourcePort,uint64_t target,const char* port) {
    GraphEdge edge;
    edge.source=source;edge.target=target;
    edge.sourcePortId=sourcePort;edge.portId=port;
    graph.edges.push_back(edge);
}

int main() {
    // 1. Descriptor self-consistency, including stable keys and ports.
    {
        const std::string problems=registryProblems();
        if(!problems.empty())fprintf(stderr,"%s",problems.c_str());
        assert(problems.empty());
        std::set<std::string> keys;
        for(const auto& spec:nodeSpecs()) assert(keys.insert(spec.key).second);
        assert(nodeSpecForKey("soundnet.voice-arranger"));
        assert(nodeTypeForKey("soundnet.oscillator")==Osc1Node);
        assert(nodeSpecForKey("soundnet.does-not-exist")==nullptr);
        assert(nodeKey(Osc1Node)&&*nodeKey(Osc1Node));
    }
    // 2. Port IDs round-trip and never depend on position.
    {
        assert(nodePortIndex(ArrangerNode,false,"high")==0);
        assert(nodePortIndex(ArrangerNode,false,"middle")==1);
        assert(strcmp(nodePortId(ArrangerNode,false,2),"low")==0);
        assert(nodePortIndex(CompressorNode,true,"sidechain")==1);
        assert(nodeInputByRole(ArpNode,RoleClock)==-1);
        assert(nodeInputByRole(VelocityNode,RoleMain,Signal::Midi)==0);
        assert(nodePortIndex(ClipNode,true,"nope")==-1);
        assert(nodePort(FilterNode,true,9)==nullptr);    // bounded access
        assert(nodePortId(FilterNode,true,9)!=nullptr);
    }
    // 3. Parameter scope is exact and stable keys are unique.
    {
        assert(nodeAcceptsParam(FilterNode,FilterCutoff));
        assert(!nodeAcceptsParam(NoiseNode,FilterCutoff));
        assert(!nodeAcceptsParam(Osc1Node,Master));
        assert(nodeAcceptsParam(StartNode,TriggerPulse));
        assert(nodeAcceptsParam(CountNode,TriggerCount));
        assert(paramIdForKey("filter.cutoff")==FilterCutoff);
        assert(strcmp(paramKey(FilterCutoff),"filter.cutoff")==0);
        assert(paramIdForKey("high.minimum")==ArrHighMin);
        assert(paramIdForKey("not.a.parameter")==-1);
        assert(nodeParamDiscrete(BitcrusherNode,CrushBits));
        assert(nodeParamDiscrete(TransposeNode,TransposeSemitones));
        assert(nodeParamDiscrete(AudioClipNode,AudioClipReverse));
        assert(nodeParamDiscrete(EchoNode,EchoPitch));
        assert(!nodeParamDiscrete(SamplerNode,SamplerRoot));   // pitch ratio is continuous
        assert(!nodeParamDiscrete(FilterNode,FilterCutoff));
        assert(discreteParam(FilterType) && !discreteParam(FilterCutoff));
        const ParamSpec* filterType=paramSpec(FilterType);
        assert(filterType&&filterType->optionCount==3&&filterType->options);
        assert(std::string(filterType->options[1])=="高通");
        assert(paramSpec(QuantizeRoot)->optionCount==12);
        assert(paramSpec(FilterCutoff)->options==nullptr);   // continuous: no enum
    }
    // 4. Same graph, two spellings: positional v3 edges and named v4 edges must
    //    compile to the same plan.
    {
        GraphSpec positional;
        positional.nodes={node(1,MidiNode),node(2,ArrangerNode),node(3,Osc1Node)};
        positional.nodes[1].parameters={{ArrHighMin,72},{ArrHighMax,95},{ArrMidMin,55},
            {ArrMidMax,71},{ArrLowMin,36},{ArrLowMax,54}};
        positional.edges.push_back({1,2,0,0});
        positional.edges.push_back({2,3,0,1});
        GraphSpec named;
        named.nodes=positional.nodes;
        connect(named,1,"notes",2,"notes");
        connect(named,2,"middle",3,"notes");
        CompiledGraph a,b;
        std::string why;
        assert(compileGraph(positional,a,&why));
        assert(compileGraph(named,b,&why));
        assert(a.nodes.size()==b.nodes.size());
        assert(a.nodes[2].inputs[0].node==b.nodes[2].inputs[0].node);
        assert(a.nodes[2].inputs[0].port==b.nodes[2].inputs[0].port);
        assert(a.nodes[2].inputs[0].port==1);
    }
    // 5. Signal compatibility is checked against the descriptor, not a list.
    {
        GraphSpec graph;
        graph.nodes={node(1,nodeTypeID("soundnet.timecode")),node(2,Osc1Node)};
        connect(graph,1,"time",2,"notes");
        CompiledGraph plan;
        std::string why;
        assert(!compileGraph(graph,plan,&why));
        assert(!why.empty());
    }
    // 6. Identical trigger edges are rejected as duplicates; distinct sources are
    //    allowed on a trigger input.
    {
        GraphSpec graph;
        graph.nodes={node(1,StartNode),node(2,StopNode),node(3,ClipNode)};
        connect(graph,1,"trigger",3,"trigger");
        connect(graph,1,"trigger",3,"trigger");
        CompiledGraph plan;
        std::string why;
        assert(compileGraph(graph,plan,&why));
        assert(plan.nodes[2].triggerInputs[0].size()==1);
        connect(graph,2,"trigger",3,"trigger");
        assert(compileGraph(graph,plan,&why));
        assert(plan.nodes[2].triggerInputs[0].size()==2);
    }
    // 6b. An ordinary input takes exactly one source: two edges into the same
    //     audio input are an error, not a silent overwrite.
    {
        GraphSpec graph;
        graph.nodes={node(1,NoiseNode),node(2,FilterNode),node(3,AudioOutputNode)};
        connect(graph,1,"audio",2,"audio");
        connect(graph,1,"audio",2,"audio");
        connect(graph,2,"audio",3,"audio");
        CompiledGraph plan;
        std::string why;
        assert(!compileGraph(graph,plan,&why));
        assert(why.find("一条连接")!=std::string::npos);
    }
    // 7. Timed feedback through a feedback-boundary port is ordered by the
    //    engine; an ordinary audio loop stays a cycle error.
    {
        GraphSpec loop;
        loop.nodes={node(1,ClipNode),node(2,CountNode),node(3,StopNode)};
        connect(loop,1,"cycle",2,"trigger");
        connect(loop,2,"trigger",3,"trigger");
        connect(loop,3,"trigger",1,"trigger");
        CompiledGraph plan;
        std::string why;
        assert(compileGraph(loop,plan,&why));
        assert(plan.order.size()==3);
        GraphSpec audio;
        audio.nodes={node(1,FilterNode),node(2,ReverbNode)};
        connect(audio,1,"audio",2,"audio");
        connect(audio,2,"audio",1,"audio");
        assert(!compileGraph(audio,plan,&why));
    }
    // 8. Unknown nodes are preserved but never executed, and the rest of the
    //    graph still compiles.
    {
        GraphSpec graph;
        GraphNode unknown;
        unknown.id=2;unknown.key="soundnet.future-node";unknown.type=0;unknown.unavailable=true;
        graph.nodes={node(1,NoiseNode),unknown,node(3,AudioOutputNode)};
        connect(graph,1,"audio",3,"audio");
        CompiledGraph plan;
        std::string why;
        assert(!compileGraph(graph,plan,&why));
        assert(compileGraph(graph,plan,&why,true));
        assert(plan.order.size()==3);
        assert(plan.nodes[1].spec==nullptr);
        assert(plan.nodes[2].inputs[0].node==0);
    }
    // 9. Bypass is a declared capability. A node without it silently loses the
    //    flag and reports a diagnostic instead of pretending to support it.
    {
        GraphSpec graph;
        GraphNode osc=node(1,Osc1Node);
        osc.bypass=true;
        graph.nodes={osc,node(2,AudioOutputNode)};
        connect(graph,1,"audio",2,"audio");
        CompiledGraph plan;
        std::string why;
        assert(compileGraph(graph,plan,&why));
        assert(plan.nodes[0].bypass==false);
        assert(!plan.diagnostics.empty());
        GraphNode filter=node(1,FilterNode);
        filter.bypass=true;
        graph.nodes[0]=filter;
        graph.edges.clear();
        assert(compileGraph(graph,plan,&why));
        assert(plan.nodes[0].bypass==true);
    }
    // 10. Every output sink has an input, every generator declares itself, and
    //     nothing relies on a numeric type ceiling.
    {
        for(const auto& spec:nodeSpecs()) {
            if(!spec.instantiable)continue;
            if(nodeHas(spec.capabilities,CapOutputSink)) assert(!spec.inputs.empty());
            if(nodeHas(spec.capabilities,CapClockSource)) assert(!spec.outputs.empty());
            if(nodeHas(spec.capabilities,CapTemporal)) assert(spec.timeMode!=TimeMode::None);
        }
        assert(validNodeType(CommentNode));
        assert(!validNodeType(0));
        assert(!validNodeType(Osc2Node));            // migration only
        assert(!validNodeType(CommentNode+1000));
        assert(FirstAppendedNode>CommentNode);
    }
    // 11. The documented project budget (4096 nodes / 16384 edges) must compile
    //     in interactive time: publishing a topology edit happens on the UI
    //     thread, so a quadratic port lookup would freeze the editor.
    {
        GraphSpec graph;
        for(int i=0;i<4;++i){GraphNode n;n.id=uint64_t(i+1);n.type=NoiseNode;n.key=nodeKey(NoiseNode);graph.nodes.push_back(n);}
        uint64_t id=5;
        while(graph.nodes.size()<maxGraphNodes) {
            GraphNode n;n.id=id++;n.type=MixNode;n.key=nodeKey(MixNode);graph.nodes.push_back(n);
        }
        const char* ports[4]={"in1","in2","in3","in4"};
        for(size_t i=4;i<graph.nodes.size()&&graph.edges.size()+4<=maxGraphEdges;++i)
            for(int p=0;p<4;++p) connect(graph,uint64_t(1+(i%4)),"audio",graph.nodes[i].id,ports[p]);
        const auto start=std::chrono::steady_clock::now();
        CompiledGraph plan;std::string why;
        assert(compileGraph(graph,plan,&why));
        const double milliseconds=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-start).count();
        assert(plan.order.size()==graph.nodes.size());
        if(milliseconds>500) {
            fprintf(stderr,"compileGraph took %.1f ms for the max budget\n",milliseconds);
            assert(false);
        }
    }
    puts("PASS: node descriptors, stable ports, parameter scope, duplicate triggers, feedback and unknown nodes");
}
