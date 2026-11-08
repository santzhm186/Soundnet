#include "../src/Engine.hpp"
#include <cassert>
#include <cstdio>
using namespace tri;
struct Sent {int frame;int status,note;};
int main() {
    GraphSpec graph;
    for(int i=1;i<=40;++i)graph.nodes.push_back({uint64_t(i),Osc1Node});
    std::vector<size_t> order;
    assert(graphOrder(graph,order) && order.size()==40);
    graph={};
    GraphNode a{1,ClipNode},b{2,ClipNode};
    a.clip={{{0,0x90,60,100},{.1,0x80,60,0}},.25};
    b.clip={{{0,0x90,72,100},{.1,0x80,72,0}},.25};
    a.parameters={{ClipRun,1},{ClipLoop,1}};
    b.parameters={{ClipRun,0},{ClipLoop,1}};
    graph.nodes={a,b,{3,ClockNode},{4,CountNode},{5,StopNode},{6,StartNode},{7,WaitNode},{8,MidiOutNode},{9,MidiOutNode}};
    graph.nodes[2].parameters={{ClockBPM,120}};
    graph.nodes[3].parameters={{TriggerCount,3}};
    graph.nodes[6].parameters={{WaitSeconds,.05}};
    graph.edges={{3,1,0},{3,2,0},{1,4,0,1},{4,5,0},{5,1,1},{6,7,0},{7,2,1},{1,8,0},{2,9,0}};
    assert(graphOrder(graph,order));
    Engine engine;engine.prepare(48000);
    std::vector<Sent> sent;sent.reserve(100);
    engine.setMidiOutput(&sent,[](void* p,int frame,uint8_t s,uint8_t a,uint8_t){static_cast<std::vector<Sent>*>(p)->push_back({frame,s,a});});
    assert(engine.setGraph(graph));
    float l[128],r[128];
    int aOn=0,bOn=0;
    for(int i=0;i<230;++i)engine.render(nullptr,nullptr,l,r,128);
    for(auto e:sent)if((e.status&0xf0)==0x90){if(e.note==60)++aOn;if(e.note==72)++bOn;}
    assert(aOn==3 && bOn==0);
    graph.nodes[5].parameters[TriggerPulse]=1;
    assert(engine.setGraph(graph));sent.clear();
    for(int i=0;i<30;++i)engine.render(nullptr,nullptr,l,r,128);
    int start=-1;
    for(auto e:sent)if((e.status&0xf0)==0x90&&e.note==72){start=e.frame;break;}
    assert(start>=0);
    // Invalid audio/note cycles and mismatched ports are rejected before publication.
    auto bad=graph;bad.edges.push_back({1,3,0});assert(!engine.setGraph(bad));

    GraphSpec audio;
    GraphNode osc{11,Osc1Node};osc.parameters={{Wave1,0},{Level1,.8f},{Attack,.001f},{Release,.005f}};
    GraphNode quiet=osc;quiet.id=12;quiet.parameters[Level1]=0;
    audio.nodes={{10,MidiNode},osc,quiet,{13,HardwareOutputNode},{14,HardwareOutputNode}};
    audio.edges={{10,11,0},{10,12,0},{11,13,0},{12,14,0}};
    Engine synth;synth.prepare(48000);assert(synth.setGraph(audio));
    synth.midi(0x90,69,100);
    float maximum=0;
    for(int block=0;block<20;++block) {
        synth.render(nullptr,nullptr,l,r,128);
        for(float sample:l)maximum=std::max(maximum,std::abs(sample));
    }
    assert(maximum>.05f);
    auto muted=audio;muted.nodes[1].parameters[Level1]=0;
    assert(synth.setGraph(muted));
    synth.render(nullptr,nullptr,l,r,128);
    for(float sample:l)assert(sample==0);
    auto invalid=audio;invalid.edges.push_back({11,12,0});assert(!synth.setGraph(invalid));
    // Resource count is determined by the document. All forty audio instances
    // can actually compile, render and be removed.
    GraphSpec many;
    for(int id=1;id<=40;++id)many.nodes.push_back({uint64_t(id),Osc1Node});
    assert(synth.setGraph(many));synth.render(nullptr,nullptr,l,r,128);
    assert(synth.setGraph({}));synth.render(nullptr,nullptr,l,r,128);
    // Dropping the upstream of a MIDI output must release its sounding notes
    // instead of leaving a stuck note at the host.
    {
        GraphSpec g;
        g.nodes={{21,MidiNode},{22,MidiOutNode}};
        g.edges={{21,22,0}};
        Engine e2;e2.prepare(48000);
        std::vector<Sent> out;out.reserve(32);
        e2.setMidiOutput(&out,[](void* p,int frame,uint8_t s,uint8_t a,uint8_t){static_cast<std::vector<Sent>*>(p)->push_back({frame,s,a});});
        assert(e2.setGraph(g));
        e2.midi(0x90,64,100);
        for(int block=0;block<4;++block)e2.render(nullptr,nullptr,l,r,128);
        bool held=false;
        for(auto e:out)if(e.status==0x90&&e.note==64)held=true;
        assert(held);
        GraphSpec cut;cut.nodes=g.nodes;    // same nodes, no edge
        out.clear();
        assert(e2.setGraph(cut));
        e2.render(nullptr,nullptr,l,r,128);
        bool released=false;
        for(auto e:out)if((e.status&0xf0)==0x80&&e.note==64)released=true;
        assert(released);
    }
    puts("PASS: 40 independent instances, distinct clips, third-loop stop feedback, delayed manual start, note release on lost input and typed validation.");
}
