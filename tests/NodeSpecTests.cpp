#include "NodeTestLookup.hpp"
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
        assert(nodeTypeForKey("soundnet.oscillator")==ntest::type("oscillator"));
        assert(nodeSpecForKey("soundnet.does-not-exist")==nullptr);
        assert(nodeKey(ntest::type("oscillator"))&&*nodeKey(ntest::type("oscillator")));
    }
    // 2. Port IDs round-trip and never depend on position.
    {
        assert(nodePortIndex(ntest::type("voice-arranger"),false,"high")==0);
        assert(nodePortIndex(ntest::type("voice-arranger"),false,"middle")==1);
        assert(strcmp(nodePortId(ntest::type("voice-arranger"),false,2),"low")==0);
        assert(nodePortIndex(ntest::type("compressor"),true,"sidechain")==1);
        assert(nodeInputByRole(ntest::type("arp"),RoleClock)==-1);
        assert(nodeInputByRole(ntest::type("velocity"),RoleMain,Signal::Midi)==0);
        assert(nodePortIndex(ntest::type("midi-clip"),true,"nope")==-1);
        assert(nodePort(ntest::type("filter"),true,9)==nullptr);    // bounded access
        assert(nodePortId(ntest::type("filter"),true,9)!=nullptr);
    }
    // 3. Parameter scope is exact and stable keys are unique.
    {
        assert(nodeAcceptsParam(ntest::type("filter"),ntest::param("filter","filter.cutoff")));
        assert(!nodeAcceptsParam(ntest::type("noise"),ntest::param("filter","filter.cutoff")));
        assert(!nodeAcceptsParam(ntest::type("oscillator"),ntest::param("audio-output","output.master")));
        assert(nodeAcceptsParam(ntest::type("start"),ntest::param("start","trigger.pulse")));
        assert(nodeAcceptsParam(ntest::type("count"),ntest::param("count","trigger.count")));
        assert(paramIdForKey("filter.cutoff")==ntest::param("filter","filter.cutoff"));
        assert(strcmp(paramKey(ntest::param("filter","filter.cutoff")),"filter.cutoff")==0);
        assert(paramIdForKey("high.minimum")==ntest::param("voice-arranger","high.minimum"));
        assert(paramIdForKey("not.a.parameter")==-1);
        assert(nodeParamDiscrete(ntest::type("bitcrusher"),ntest::param("bitcrusher","crush.bits")));
        assert(nodeParamDiscrete(ntest::type("transpose"),ntest::param("transpose","transpose.semitones")));
        assert(nodeParamDiscrete(ntest::type("audio-clip"),ntest::param("audio-clip","audioClip.reverse")));
        assert(nodeParamDiscrete(ntest::type("note-echo"),ntest::param("note-echo","echo.pitch")));
        assert(!nodeParamDiscrete(ntest::type("sampler"),ntest::param("sampler","sampler.root")));   // pitch ratio is continuous
        assert(!nodeParamDiscrete(ntest::type("filter"),ntest::param("filter","filter.cutoff")));
        assert(discreteParam(ntest::param("filter","filter.type")) && !discreteParam(ntest::param("filter","filter.cutoff")));
        const ParamSpec* filterType=paramSpec(ntest::param("filter","filter.type"));
        assert(filterType&&filterType->optionCount==3&&filterType->options);
        // Labels follow the DSP selector: 0 low-pass, 1 band-pass, 2 high-pass.
        // nodes/filter/Tests.cpp also verifies the corresponding DC response.
        assert(std::string(filterType->options[1])=="带通");
        assert(std::string(filterType->options[2])=="高通");
        assert(paramSpec(ntest::param("scale-quantize","quantize.root"))->optionCount==12);
        assert(paramSpec(ntest::param("filter","filter.cutoff"))->options==nullptr);   // continuous: no enum
    }
    // 4. Same graph, two spellings: positional v3 edges and named v4 edges must
    //    compile to the same plan.
    {
        GraphSpec positional;
        positional.nodes={node(1,ntest::type("midi-input")),node(2,ntest::type("voice-arranger")),node(3,ntest::type("oscillator"))};
        positional.nodes[1].parameters={{ntest::param("voice-arranger","high.minimum"),72},{ntest::param("voice-arranger","high.maximum"),95},{ntest::param("voice-arranger","middle.minimum"),55},
            {ntest::param("voice-arranger","middle.maximum"),71},{ntest::param("voice-arranger","low.minimum"),36},{ntest::param("voice-arranger","low.maximum"),54}};
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
        graph.nodes={node(1,nodeTypeID("soundnet.timecode")),node(2,ntest::type("oscillator"))};
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
        graph.nodes={node(1,ntest::type("start")),node(2,ntest::type("stop")),node(3,ntest::type("midi-clip"))};
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
        graph.nodes={node(1,ntest::type("noise")),node(2,ntest::type("filter")),node(3,ntest::type("audio-output"))};
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
        loop.nodes={node(1,ntest::type("midi-clip")),node(2,ntest::type("count")),node(3,ntest::type("stop"))};
        connect(loop,1,"cycle",2,"trigger");
        connect(loop,2,"trigger",3,"trigger");
        connect(loop,3,"trigger",1,"trigger");
        CompiledGraph plan;
        std::string why;
        assert(compileGraph(loop,plan,&why));
        assert(plan.order.size()==3);
        GraphSpec audio;
        audio.nodes={node(1,ntest::type("filter")),node(2,ntest::type("reverb"))};
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
        graph.nodes={node(1,ntest::type("noise")),unknown,node(3,ntest::type("audio-output"))};
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
        GraphNode osc=node(1,ntest::type("oscillator"));
        osc.bypass=true;
        graph.nodes={osc,node(2,ntest::type("audio-output"))};
        connect(graph,1,"audio",2,"audio");
        CompiledGraph plan;
        std::string why;
        assert(compileGraph(graph,plan,&why));
        assert(plan.nodes[0].bypass==false);
        assert(!plan.diagnostics.empty());
        GraphNode filter=node(1,ntest::type("filter"));
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
        assert(validNodeType(ntest::type("comment")));
        assert(!validNodeType(0));
        assert(!validNodeType(4)); // former numeric-only node IDs are not SDK2 modules
        assert(!validNodeType(nodeTypeID("tests.not-registered")));
        for(const auto& spec:nodeSpecs())assert(spec.type==nodeTypeID(spec.key));
    }
    // 11. The documented project budget (4096 nodes / 16384 edges) must compile
    //     in interactive time: publishing a topology edit happens on the UI
    //     thread, so a quadratic port lookup would freeze the editor.
    {
        GraphSpec graph;
        for(int i=0;i<4;++i){GraphNode n;n.id=uint64_t(i+1);n.type=ntest::type("noise");n.key=nodeKey(ntest::type("noise"));graph.nodes.push_back(n);}
        uint64_t id=5;
        while(graph.nodes.size()<maxGraphNodes) {
            GraphNode n;n.id=id++;n.type=ntest::type("mixer");n.key=nodeKey(ntest::type("mixer"));graph.nodes.push_back(n);
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
