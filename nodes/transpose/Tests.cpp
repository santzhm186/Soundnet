#include "../../src/Engine.hpp"
#include <cassert>
#include <cstdio>

static void testClampedNotes(int semitones, uint8_t first, uint8_t second, uint8_t boundary) {
    tri::GraphSpec graph;
    tri::GraphNode transpose{2,tri::TransposeNode};
    transpose.nodeVersion=tri::nodeSpec(tri::TransposeNode)->nodeVersion;
    transpose.parameters={{tri::TransposeSemitones,float(semitones)}};
    graph.nodes={{1,tri::MidiNode},transpose,{3,tri::MidiOutNode}};
    graph.edges={{1,2,0},{2,3,0}};
    auto engine=std::make_unique<tri::Engine>();
    engine->prepare(48000);
    assert(engine->setGraph(graph));

    struct Event {uint8_t status,pitch,velocity;};
    std::vector<Event> events;
    events.reserve(8);
    engine->setMidiOutput(&events,[](void* sink,int,uint8_t status,uint8_t pitch,uint8_t velocity) {
        static_cast<std::vector<Event>*>(sink)->push_back({status,pitch,velocity});
    });
    auto render=[&] {
        float left[3]{},right[3]{};
        engine->render(nullptr,nullptr,left,right,3);
    };
    engine->midi(0x93,first,100);
    engine->midi(0x93,second,90);
    engine->midi(0x94,second,80);
    render();
    assert(events.size()==2);
    assert(events[0].status==0x93 && events[0].pitch==boundary && events[0].velocity==100);
    assert(events[1].status==0x94 && events[1].pitch==boundary && events[1].velocity==80);
    events.clear();
    engine->midi(0x83,first,0);
    render();
    assert(events.empty());
    engine->midi(0x94,second,0);
    render();
    assert(events.size()==1 && events[0].status==0x84 && events[0].pitch==boundary);
    events.clear();
    engine->midi(0x83,second,0);
    render();
    assert(events.size()==1 && events[0].status==0x83 && events[0].pitch==boundary);

    // Bypass and re-enable while both original pitches are still held.
    engine->midi(0x93,first,100);
    engine->midi(0x93,second,90);
    render();
    events.clear();
    graph.nodes[1].bypass=true;
    assert(engine->setGraph(graph));
    render();
    assert(events.size()==3);
    assert(events[0].status==0x83 && events[0].pitch==boundary);
    assert(events[1].status==0x93 && events[1].pitch==std::min(first,second));
    assert(events[2].status==0x93 && events[2].pitch==std::max(first,second));
    events.clear();
    graph.nodes[1].bypass=false;
    assert(engine->setGraph(graph));
    render();
    assert(events.size()==3 && events[2].status==0x93 && events[2].pitch==boundary);
    events.clear();
    engine->setProcessingEnabled(false);
    render();
    assert(events.size()==1 && events[0].status==0x83 && events[0].pitch==boundary);
    events.clear();
    engine->setProcessingEnabled(true);
    render();
    assert(events.empty());
}

int main() {
    testClampedNotes(24,103,127,127);
    testClampedNotes(-24,24,0,0);
    std::puts("PASS: transpose keeps clamped notes held until the last release on each channel");
}
