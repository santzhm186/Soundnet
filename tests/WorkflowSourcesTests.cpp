#include "../src/Engine.hpp"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace tri;
namespace {
constexpr int rate=48000;
std::vector<float> render(Engine& engine,int count) {
    std::vector<float> l(count),r(count);
    engine.render(nullptr,nullptr,l.data(),r.data(),count);
    return l;
}
double peak(const std::vector<float>& audio,int first=512) {
    double result=0;
    for (size_t i=first;i<audio.size();++i) result=std::max(result,double(std::abs(audio[i])));
    return result;
}
double amplitude(const std::vector<float>& audio,double hz,int start,int count) {
    double re=0,im=0;
    for(int i=0;i<count;++i) {
        double phase=2*3.141592653589793*hz*i/rate;
        re+=audio[start+i]*std::cos(phase);im+=audio[start+i]*std::sin(phase);
    }
    return 2*std::hypot(re,im)/count;
}
std::unique_ptr<Engine> instrument() {
    auto engine=std::make_unique<Engine>();
    engine->set(RouteOutputIn,Osc1Node);
    engine->set(Wave1,0);engine->set(Level1,1);engine->set(Master,1);
    engine->set(Attack,.001f);engine->set(Decay,.005f);
    engine->set(Sustain,1);engine->set(Release,.005f);
    return engine;
}
void portIsolation() {
    auto e=instrument();
    e->set(RouteOsc1In,MidiNode);
    e->dawMidi(0x90,69,127);
    assert(peak(render(*e,4096))>.2); // old patches receive both sources
    e->set(MidiLocalOnly,1);
    assert(peak(render(*e,4096))<1e-8);
    e->midi(0x90,69,90);
    assert(peak(render(*e,4096))>.14);
    e->dawMidi(0x80,69,0);
    assert(peak(render(*e,4096))>.14);
    e->set(RouteOsc1In,DawMidiNode);
    assert(peak(render(*e,4096))<1e-8);
    e->dawMidi(0x90,69,127);
    e->midi(0x80,69,0);
    assert(peak(render(*e,4096))>.2);
    e->dawMidi(0xb0,64,127);
    e->dawMidi(0x80,69,0);
    assert(peak(render(*e,4096))>.2);
    e->dawMidi(0xb0,64,0);
    assert(peak(render(*e,4096))<1e-8);
    // Repeated note-ons and independent pedals on matching MIDI channels.
    e->dawMidi(0x90,69,90);e->dawMidi(0x90,69,120);e->dawMidi(0x80,69,0);
    assert(peak(render(*e,4096))>.2);
    e->dawMidi(0x80,69,0);assert(peak(render(*e,4096))<1e-8);
}
MidiClip clip() {
    return {{{0.,0x90,69,127},{.5,0x80,69,0},{1.,0x90,81,127},{1.5,0x80,81,0}},2.};
}
void clipAndClock() {
    auto e=instrument();e->set(RouteOsc1In,ClipNode);e->set(RouteClipClock,ClockNode);
    e->set(ClockSync,0);e->set(ClockBPM,120);e->set(ClipRun,1);
    assert(e->setClip(clip()));
    e->setHostTempo(60); e->setHostTransport(false,0);
    auto dry=render(*e,96000);
    assert(amplitude(dry,440,512,8192)>.24);
    assert(amplitude(dry,880,24576,8192)>.24);
    assert(amplitude(dry,440,48512,8192)>.24);
    e->set(ClockSync,1); // same patched clock now follows the DAW
    assert(peak(render(*e,4096))<1e-8);
    e->setHostTransport(true,1.25);
    assert(amplitude(render(*e,8192),880,512,6656)>.24); // chase a held note
    e->setHostTransport(false,1.45);
    assert(peak(render(*e,4096))<1e-8);
    e->setHostTransport(true,0.);
    assert(amplitude(render(*e,8192),440,512,6656)>.24);
    e->setHostTransport(true,1.75);
    assert(peak(render(*e,4096))<1e-8);
    e->set(ClipLoop,0);e->setHostTransport(true,2.25);
    assert(peak(render(*e,8192))<1e-8);
    e->clearHostTransport();e->setHostTempo(0);
    e->set(ClipRun,0);render(*e,64);e->set(ClipRun,1);
    dry=render(*e,100000);
    assert(amplitude(dry,440,512,6656)>.24);
    assert(peak(dry,49000)<1e-8); // one-shot releases the final note and remains silent
    auto bad=clip();bad.events[1].beat=-1.;
    assert(!e->setClip(bad));
}
void modelAndGraph() {
    auto e=instrument(); auto graph=defaultRouting;
    graph[routeIndex(RouteOutputIn)]=Osc1Node;
    graph[routeIndex(RouteOsc1In)]=ModelNode;
    graph[routeIndex(RouteModelIn)]=ClipNode;
    graph[routeIndex(RouteClipClock)]=ClockNode;
    assert(validRouting(graph) && unpackRouting(packRouting(graph))==graph);
    assert(!canConnect(graph,RouteClipClock,DawMidiNode));
    assert(!canConnect(graph,RouteModelIn,EQNode));
    graph[routeIndex(RouteModelIn)]=ArpNode;
    assert(!canConnect(graph,RouteArpIn,ModelNode));
    graph[routeIndex(RouteModelIn)]=DawMidiNode;
    assert(e->setRouting(graph));
    NoteModel model;model.pitches[69]=81;assert(e->setModel(model));
    e->dawMidi(0x90,69,127);
    auto audio=render(*e,12000);
    assert(amplitude(audio,880,512,8192)>.24 && amplitude(audio,440,512,8192)<.004);
    model.pitches[69]=57;assert(e->setModel(model));
    assert(amplitude(render(*e,12000),220,512,8192)>.24); // edit under held notes
    model.pitches[69]=128;assert(!e->setModel(model));
    e->dawMidi(0x80,69,0);assert(peak(render(*e,4096))<1e-8);
    // Verify compact graph encoding for each compatible source on each input.
    for(size_t i=0;i<graph.size();++i) for(int n=0;n<=ModelNode;++n) {
        if(!compatibleSource(routeID(i),n)) continue;
        auto changed=graph;changed[i]=n;
        assert(unpackRouting(packRouting(changed))==changed);
    }
}
struct Sent { int frame; uint8_t status,note,velocity; };
void midiOutput() {
    auto e=instrument();
    std::vector<Sent> sent;
    sent.reserve(64);
    e->setMidiOutput(&sent,[](void* ptr,int frame,uint8_t status,uint8_t n,uint8_t v) {
        static_cast<std::vector<Sent>*>(ptr)->push_back({frame,status,n,v});
    });
    Routing graph{};
    graph[routeIndex(RouteMidiOutIn)]=ModelNode;
    graph[routeIndex(RouteModelIn)]=ClipNode;
    graph[routeIndex(RouteClipClock)]=ClockNode;
    assert(e->setRouting(graph));
    NoteModel model;model.pitches[69]=57;assert(e->setModel(model));
    MidiClip sequence{{{.1,0x93,69,88},{.125,0x83,69,0}},1.};
    assert(e->setClip(sequence));
    e->set(ClockSync,0);e->set(ClockBPM,120);e->set(ClipRun,1);
    assert(peak(render(*e,4096))==0.); // the graph has no audio path
    assert(sent.size()==2 && sent[0].frame==2400 && sent[1].frame==3000);
    assert(sent[0].status==0x93 && sent[0].note==57 && sent[0].velocity==88);
    assert(sent[1].status==0x83 && sent[1].note==57);
    render(*e,4096);
    assert(sent.size()==2);
    e->set(RouteModelIn,DawMidiNode);e->dawMidi(0x95,69,99);
    assert(sent.size()==3 && sent.back().status==0x95 && sent.back().frame==0);
    e->set(RouteMidiOutIn,NoNode);render(*e,64);
    assert(sent.size()==4 && sent.back().status==0x85 && sent.back().note==57);
    e->set(RouteMidiOutIn,ModelNode);render(*e,64);
    assert(sent.size()==5 && sent.back().status==0x95);
    e->reset();
    assert(sent.size()==6 && sent.back().status==0x85);
    render(*e,64);assert(sent.size()==6);
    e->setMidiOutput(nullptr,nullptr);
}

}
int main() {
    portIsolation();clipAndClock();modelAndGraph();midiOutput();
    std::puts("PASS: independent DAW and local MIDI, MIDI clip loops, one-shot, DAW stop/seek/chase, local BPM, model remapping, graph encoding and sample-accurate MIDI-only output.");
}
