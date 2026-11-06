#pragma once
#include "InstanceGraph.hpp"

namespace tri {
// Legacy parameter IDs continue to load older AU presets. New documents own their
// instance graph and publish complete topology edits through a separate plan.
class Engine : public LegacyEngine {
    InstanceGraph graph;
public:
    bool setGraph(const GraphSpec& spec) {return graph.publish(spec);}
    void clearGraph() {graph.disable();}
    void setHosted(bool value) {graph.setHosted(value);}
    void prepare(double sr) {LegacyEngine::prepare(sr);graph.prepare(sr);}
    void reset() {LegacyEngine::reset();graph.reset();}
    void panic() {LegacyEngine::panic();graph.panic();}
    void midi(uint8_t s,uint8_t a,uint8_t b) {
        if(graph.enabled())graph.midi(false,s,a,b);else LegacyEngine::midi(s,a,b);
    }
    void dawMidi(uint8_t s,uint8_t a,uint8_t b) {
        if(graph.enabled())graph.midi(true,s,a,b);else LegacyEngine::dawMidi(s,a,b);
    }
    void setHostTempo(double bpm) {LegacyEngine::setHostTempo(bpm);graph.setHostTempo(bpm);}
    void setHostTransport(bool playing,double beat) {LegacyEngine::setHostTransport(playing,beat);graph.setHostTransport(playing,beat);}
    void clearHostTransport() {LegacyEngine::clearHostTransport();graph.clearHostTransport();}
    void setMidiOutput(void* ptr,void (*fn)(void*,int,uint8_t,uint8_t,uint8_t)) {
        LegacyEngine::setMidiOutput(ptr,fn);graph.setMidiOutput(ptr,fn);
    }
    void render(const float* l,const float* r,float* outL,float* outR,int n) {
        if(graph.enabled())graph.render(l,r,outL,outR,n);
        else LegacyEngine::render(l,r,outL,outR,n);
    }
};
}
