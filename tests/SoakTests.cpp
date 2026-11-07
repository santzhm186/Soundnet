#include "../src/Engine.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace tri;

static void add(GraphSpec& g,uint64_t id,int type,std::initializer_list<std::pair<int,float>> p={}) {
    GraphNode n;n.id=id;n.type=type;n.x=float(id*10);n.y=0;
    for(auto& kv:p)n.parameters[kv.first]=kv.second;
    g.nodes.push_back(n);
}
static void connect(GraphSpec& g,uint64_t a,uint64_t b,int port=0) {
    GraphEdge e;e.source=a;e.target=b;e.port=port;g.edges.push_back(e);
}
int main(){
    GraphSpec graph;
    add(graph,1,MidiNode);
    std::vector<uint64_t> osc;
    for(int i=0;i<40;++i){
        uint64_t id=2+i;osc.push_back(id);
        add(graph,id,Osc1Node,{{Wave1,float(i%4)},{Level1,.08f},{Tune1,float((i%7)-3)}});
        connect(graph,1,id);
    }
    // Four-input mix tree: 10 + 3 + 1 mixers.
    std::vector<uint64_t> layer;
    uint64_t next=100;
    for(size_t i=0;i<osc.size();i+=4){
        add(graph,next,MixNode);
        for(int j=0;j<4 && i+j<osc.size();++j)connect(graph,osc[i+j],next,j);
        layer.push_back(next++);
    }
    while(layer.size()>1){
        std::vector<uint64_t> upper;
        for(size_t i=0;i<layer.size();i+=4){
            add(graph,next,MixNode);
            for(int j=0;j<4 && i+j<layer.size();++j)connect(graph,layer[i+j],next,j);
            upper.push_back(next++);
        }
        layer=upper;
    }
    add(graph,next,HardwareOutputNode,{{Master,.7f}});connect(graph,layer[0],next,0);

    Engine engine;assert(engine.setGraph(graph));engine.prepare(48000);
    engine.midi(0x90,60,100);
    std::vector<float> left(512),right(512);
    double peak=0;int blocks=20*48000/512;
    for(int b=0;b<blocks;++b){
        if(b && b%(48000/512)==0){engine.panic();engine.render(nullptr,nullptr,left.data(),right.data(),512);engine.midi(0x90,60,100);}
        engine.render(nullptr,nullptr,left.data(),right.data(),512);
        for(int i=0;i<512;++i){
            if(!std::isfinite(left[i])||!std::isfinite(right[i])){puts("non-finite sample");return 1;}
            peak=std::max(peak,double(std::abs(left[i])));
        }
        if(b%400==399)assert(engine.setGraph(graph));
    }
    assert(peak>0.001 && peak<=1.001);
    printf("PASS: 20 s soak of 56-node graph, peak %.4f\n",peak);
    return 0;
}
