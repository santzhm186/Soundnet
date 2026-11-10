#include "NodeTestLookup.hpp"
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
    GraphNode node;node.id=id;node.type=type;node.nodeVersion=nodeSpec(type)->nodeVersion;node.parameters=std::move(parameters);graph.nodes.push_back(node);
}
static void connect(GraphSpec& graph, InstanceID source, InstanceID target, int port = 0, int sourcePort = 0) {
    graph.edges.push_back({source,target,port,sourcePort});
}

int main() {
    GraphSpec graph;
    add(graph,1,ntest::type("midi-input"));
    add(graph,2,ntest::type("oscillator"),{{ntest::param("oscillator","osc1.wave"),0},{ntest::param("oscillator","osc1.level"),.3f},{ntest::param("oscillator","envelope.attack"),.001f},{ntest::param("oscillator","envelope.sustain"),1}});
    add(graph,3,ntest::type("audio-output"),{{ntest::param("audio-output","output.master"),.5f}});
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
        next.nodes[2].parameters[ntest::param("audio-output","output.master")]=.1f+.5f*float(i%7)/7.f;
        if(!engine->setGraph(next))failed=true;
        if(i%5==0)engine->panic();
        if(i%3==0){engine->setProcessingEnabled(false);engine->setProcessingEnabled(true);}
        engine->midi(0x90,60,100);
        engine->midi(0x80,60,0);
    }
    render.join();
    if(failed)return 2;
    // Regression: reset used to walk controlNodes while publication replaced
    // that map. Keep adding/removing task nodes while the render thread handles
    // stop and panic requests. Only published snapshots may be visited there.
    {
        auto changing=std::make_unique<Engine>();changing->prepare(48000);
        GraphSpec tasks;
        for(int i=0;i<16;++i)add(tasks,100+i,nodeTypeForKey("soundnet.yue2"));
        if(!changing->setGraph(tasks))return 3;
        std::atomic<bool> started{false},done{false};
        std::atomic<unsigned> blocks{0};
        std::thread audio([&] {
            float left[128]{},right[128]{};
            started.store(true,std::memory_order_release);
            while(!done.load(std::memory_order_acquire)) {
                changing->render(nullptr,nullptr,left,right,128);
                blocks.fetch_add(1,std::memory_order_relaxed);
                for(float value:left)if(!std::isfinite(value))failed=true;
            }
        });
        while(!started.load(std::memory_order_acquire))std::this_thread::yield();
        for(int edit=0;edit<500;++edit) {
            auto next=tasks;next.nodes.back().id=1000+edit;
            if(!changing->setGraph(next))failed=true;
            changing->panic();
            if(edit%2==0)changing->setProcessingEnabled(false);
            else changing->setProcessingEnabled(true);
            changing->stopNode(next.nodes.back().id);
        }
        done.store(true,std::memory_order_release);audio.join();
        if(!blocks.load()||failed)return 4;
    }
    puts("PASS: concurrent render, publish, panic and MIDI input");
    return 0;
}
