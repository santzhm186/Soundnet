#include "../src/Engine.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace tri;

static size_t add(GraphSpec& graph, InstanceID id, int type, std::map<int,float> parameters={}) {
    GraphNode node;node.id=id;node.type=type;node.parameters=std::move(parameters);
    graph.nodes.push_back(std::move(node));
    return graph.nodes.size()-1;
}
static void connect(GraphSpec& graph,InstanceID source,InstanceID target,int port=0,int sourcePort=0) {
    graph.edges.push_back({source,target,port,sourcePort});
}
static double rms(const std::vector<float>& audio,size_t begin,size_t end) {
    double sum=0;for(size_t i=begin;i<end;++i)sum+=double(audio[i])*audio[i];
    return std::sqrt(sum/std::max<size_t>(1,end-begin));
}
static double tone(const std::vector<float>& audio,double hz,size_t begin,size_t count) {
    double re=0,im=0;
    for(size_t i=0;i<count;++i){double phase=2*3.14159265358979323846*hz*double(begin+i)/48000.0;re+=audio[begin+i]*std::cos(phase);im+=audio[begin+i]*std::sin(phase);}
    return std::hypot(re,im)/double(count);
}

int main() {
    // New node types are accepted and their parameters are type-scoped.
    {
        GraphSpec graph;
        add(graph,1,NoiseNode,{{NoiseLevel,.5f},{NoiseType,0}});
        add(graph,2,FilterNode,{{FilterType,0},{FilterCutoff,1000},{FilterResonance,1}});
        add(graph,3,ReverbNode,{{ReverbMix,.4f},{ReverbSize,.5f}});
        add(graph,4,OutputNode,{{Master,1}});
        connect(graph,1,2);connect(graph,2,3);connect(graph,3,4);
        std::vector<size_t> order;
        assert(graphOrder(graph,order));
        assert(!validGraphParam(NoiseNode,FilterCutoff));
        assert(!validGraphParam(FilterNode,ReverbMix));
        assert(validGraphParam(NoiseNode,NoiseType));
        graph.nodes[0].parameters[FilterCutoff]=1000;
        assert(!graphOrder(graph,order));
        graph.nodes[0].parameters.erase(FilterCutoff);
        graph.nodes[1].parameters[FilterCutoff]=30000;
        assert(!graphOrder(graph,order));
        graph.nodes[1].parameters[FilterCutoff]=1000;
        graph.nodes[0].type=99;
        assert(!graphOrder(graph,order));
    }

    // Noise -> filter -> reverb -> output produces finite, non-silent audio.
    {
        GraphSpec graph;
        add(graph,1,NoiseNode,{{NoiseLevel,.6f},{NoiseType,1}});
        add(graph,2,FilterNode,{{FilterType,0},{FilterCutoff,1800},{FilterResonance,1.2f}});
        add(graph,3,ReverbNode,{{ReverbMix,.5f},{ReverbSize,.7f},{ReverbDamping,.2f},{ReverbWidth,1}});
        add(graph,4,OutputNode,{{Master,1}});
        connect(graph,1,2);connect(graph,2,3);connect(graph,3,4);
        Engine engine;assert(engine.setGraph(graph));engine.prepare(48000);
        std::vector<float> left(48000),right(48000);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        double level=rms(left,0,left.size());
        assert(level>.005 && level<2);
        for(float sample:left)assert(std::isfinite(sample));
        puts("noise/filter/reverb graph renders");
    }

    // Bypassing an inline filter must restore the unfiltered level.
    {
        GraphSpec graph;
        add(graph,10,MidiNode);
        add(graph,1,Osc1Node,{{Wave1,0},{Level1,1},{Attack,.001f},{Decay,.005f},{Sustain,1},{Release,.01f}});
        add(graph,2,FilterNode,{{FilterType,0},{FilterCutoff,180},{FilterResonance,.7f}});
        add(graph,3,OutputNode,{{Master,1}});
        connect(graph,10,1);connect(graph,1,2);connect(graph,2,3);
        Engine engine;assert(engine.setGraph(graph));engine.prepare(48000);
        engine.midi(0x90,69,110);
        std::vector<float> left(48000),right(48000);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        double filtered=rms(left,12000,42000);
        double low=tone(left,440,12000,24000);
        graph.nodes[2].bypass=true;
        assert(engine.setGraph(graph));
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        double bypassed=rms(left,12000,42000);
        double high=tone(left,440,12000,24000);
        assert(bypassed>filtered*1.5);
        assert(high>low*2);
        for(float sample:left)assert(std::isfinite(sample));
        puts("filter bypass restores the dry signal");
    }

    // A bypassed output node is silent.
    {
        GraphSpec graph;
        add(graph,1,NoiseNode,{{NoiseLevel,1}});
        add(graph,2,OutputNode,{{Master,1}});
        connect(graph,1,2);
        graph.nodes[1].bypass=true;
        Engine engine;assert(engine.setGraph(graph));engine.prepare(48000);
        std::vector<float> left(4096),right(4096);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        assert(rms(left,0,left.size())==0);
        puts("output bypass is silent");
    }
    puts("PASS: filter, reverb, noise, bypass and type-scoped validation.");
}
