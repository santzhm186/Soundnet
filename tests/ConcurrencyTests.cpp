// Render, graph publication, panic requests and MIDI input happen on different
// threads in the host. This test exercises that combination under TSan/ASan too.
#include "../src/Engine.hpp"
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <thread>
#include <vector>
using namespace tri;

static void add(GraphSpec& graph, InstanceID id, int type, std::map<int, float> parameters = {}) {
    GraphNode node;node.id=id;node.type=type;node.parameters=std::move(parameters);graph.nodes.push_back(node);
}
static void connect(GraphSpec& graph, InstanceID source, InstanceID target, int port = 0, int sourcePort = 0) {
    graph.edges.push_back({source,target,port,sourcePort});
}

int main() {
    GraphSpec graph;
    add(graph,1,MidiNode);
    add(graph,2,Osc1Node,{{Wave1,0},{Level1,.3f},{Attack,.001f},{Sustain,1}});
    add(graph,3,OutputNode,{{Master,.5f}});
    connect(graph,1,2);connect(graph,2,3);
    auto engine=std::make_unique<Engine>();
    if(!engine->setGraph(graph))return 1;
    engine->prepare(48000);
    std::atomic<bool> failed{false};
    std::thread render([&] {
        std::vector<float> left(512),right(512);
        for(int block=0;block<400;++block) {
            engine->render(nullptr,nullptr,left.data(),right.data(),512);
            for(float sample:left)if(!std::isfinite(sample))failed=true;
            for(float sample:right)if(!std::isfinite(sample))failed=true;
        }
    });
    for(int i=0;i<80;++i) {
        GraphSpec next=graph;
        next.nodes[2].parameters[Master]=.1f+.5f*float(i%7)/7.f;
        if(!engine->setGraph(next))failed=true;
        if(i%5==0)engine->panic();
        if(i%3==0){engine->setProcessingEnabled(false);engine->setProcessingEnabled(true);}
        engine->midi(0x90,60,100);
        engine->midi(0x80,60,0);
    }
    render.join();
    if(failed)return 2;
    puts("PASS: concurrent render, publish, panic and MIDI input");
    return 0;
}
