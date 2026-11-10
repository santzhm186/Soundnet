#pragma once
#include "LegacyEngine.hpp"
#include "NodeSpec.hpp"
#include "InstanceGraph.hpp"

namespace tri {
// Historical AU parameter storage remains for the host property bridge. Production
// hosts execute only the instance graph; an empty graph never enables built-in DSP.
class Engine : public LegacyEngine {
    InstanceGraph graph;
    std::atomic<bool> processing{true};
    std::atomic<uint64_t> stopGeneration{0};
    std::atomic<bool> legacyResetRequested{false},legacyPanicRequested{false};
    // Legacy MIDI is delivered on the audio thread by both host adapters. Apply
    // pending transport reset before fresh resumed notes, not after them.
    void applyLegacyRequests() {
        if(legacyPanicRequested.exchange(false,std::memory_order_acq_rel))LegacyEngine::panic();
        if(legacyResetRequested.exchange(false,std::memory_order_acq_rel))LegacyEngine::reset();
    }
public:
    bool setGraph(const GraphSpec& spec,bool allowUnknownNodes=false) {return graph.publish(spec,allowUnknownNodes);}
    void seekTimecode(uint64_t frame) {graph.seekTimecode(frame);}
    KernelTimecode timecodeSnapshot() const {return graph.timecodeSnapshot();}
    void clearGraph() {graph.disable();}
    void prepare(double sr) {LegacyEngine::prepare(sr);graph.prepare(sr);}
    void reset() {LegacyEngine::reset();graph.reset();}
    // UI/control threads only publish requests. Destructive state changes run
    // at the next audio boundary, including a stop/resume between two blocks.
    void panic() {legacyPanicRequested.store(true,std::memory_order_release);graph.panic();}
    void stopNode(InstanceID id) {graph.requestNodeStop(id);}
    uint64_t startMidiCapture(InstanceID node,int tempoParameter) {return graph.startMidiCapture(node,tempoParameter);}
    std::shared_ptr<MidiInputCapture> finishMidiCapture(uint64_t token) {return graph.finishMidiCapture(token);}
    bool processingEnabled() const {return processing.load(std::memory_order_acquire);}
    uint64_t processingGeneration() const {return stopGeneration.load(std::memory_order_acquire);}
    void setProcessingEnabled(bool enabled) {
        if(!enabled) {
            processing.store(false,std::memory_order_release);
            stopGeneration.fetch_add(1,std::memory_order_acq_rel);
            legacyResetRequested.store(true,std::memory_order_release);
        }
        graph.setProcessingEnabled(enabled);
        processing.store(enabled,std::memory_order_release);
    }
    void midi(uint8_t s,uint8_t a,uint8_t b) {
        if(!processingEnabled())return;
        if(graph.enabled())graph.midi(false,s,a,b);
#ifndef SOUNDNET_DYNAMIC_NODES
        else {applyLegacyRequests();LegacyEngine::midi(s,a,b);}
#endif
    }
    void dawMidi(uint8_t s,uint8_t a,uint8_t b) {
        if(!processingEnabled())return;
        if(graph.enabled())graph.midi(true,s,a,b);
#ifndef SOUNDNET_DYNAMIC_NODES
        else {applyLegacyRequests();LegacyEngine::dawMidi(s,a,b);}
#endif
    }
    void setHostTempo(double bpm) {LegacyEngine::setHostTempo(bpm);graph.setHostTempo(bpm);}
    void setHostTransport(bool playing,double beat) {LegacyEngine::setHostTransport(playing,beat);graph.setHostTransport(playing,beat);}
    void setHostTimecode(bool playing,uint64_t frame) {graph.setHostTimecode(playing,frame);}
    void clearHostTransport() {LegacyEngine::clearHostTransport();graph.clearHostTransport();}
    void setMidiOutput(void* ptr,void (*fn)(void*,int,uint8_t,uint8_t,uint8_t)) {
        LegacyEngine::setMidiOutput(ptr,fn);graph.setMidiOutput(ptr,fn);
    }
    void render(const float* l,const float* r,float* outL,float* outR,int n) {
        if(graph.enabled())graph.render(l,r,outL,outR,n);
        else {
#ifdef SOUNDNET_DYNAMIC_NODES
            if(n>0&&outL&&outR){std::fill_n(outL,n,0.f);std::fill_n(outR,n,0.f);}
#else
            applyLegacyRequests();
            if(!processingEnabled()) {
                if(n>0 && outL && outR){std::fill_n(outL,n,0.f);std::fill_n(outR,n,0.f);}
                return;
            }
            LegacyEngine::render(l,r,outL,outR,n);
#endif
        }
    }
};
}
