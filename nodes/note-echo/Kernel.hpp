#pragma once
#include "Parameters.hpp"
#include "../../platform/runtime/MidiNoteState.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace tri::nodes::note_echo {
class Kernel final : public NodeKernel {
    struct Event {uint64_t due=0;uint8_t status=0,a=0,b=0;int step=0;};
    std::array<Event,4096> queue{};size_t size=0;std::array<uint16_t,2048> counts{};
    MidiNoteState output;double rate=48000,time=.25,feedback=.5;int repeats=3,pitch=0;uint64_t clock=0;bool dirty=true,lastBypass=false;
    void send(KernelOutput* out,KernelMidiEvent e) noexcept {
        const int kind=e.status&0xf0,k=int(e.a)*16+(e.status&15);if(e.a>127||e.b>127)return;
        if(kind==0x90&&e.b) {
            // Repeated echoes of a held pitch articulate a fresh attack while
            // reference counts keep an earlier/later release from cutting it.
            const size_t needed=output.notes[k].velocity?2:1;
            if(!out||out->midiCapacity-out->midiCount<needed){if(out)out->overflow=true;dirty=true;return;}
            if(output.notes[k].velocity)output.emit(out,{e.frame,uint8_t(0x80|(e.status&15)),e.a,0});
            if(!output.emit(out,e))return;
            if(counts[k]<65535)++counts[k];
        }else if(kind==0x80||kind==0x90) {
            if(counts[k]&&!--counts[k])output.emit(out,e);
        }else output.emit(out,e);
    }
    void schedule(KernelOutput* out,Event e) noexcept {
        if(size==queue.size()){if(out)out->overflow=true;dirty=true;return;}
        size_t p=size++;while(p&&queue[p-1].due>e.due){queue[p]=queue[p-1];--p;}queue[p]=e;
    }
    void clear(KernelOutput* out) noexcept {output.release(out);counts.fill(0);size=0;}
public:
    void prepare(double r,int) override {rate=r>0?r:48000;output.clear();counts.fill(0);size=0;clock=0;dirty=true;}
    void configure(const KernelConfig& c) override {
        time=std::clamp(double(c.value(EchoTime,.25f)),.05,2.0);feedback=std::clamp(double(c.value(EchoFeedback,.5f)),0.0,1.0);
        repeats=std::clamp(int(c.value(EchoRepeats,3)),1,8);pitch=std::clamp(int(c.value(EchoPitch,0)),-12,12);dirty=true;
    }
    void reset(KernelResetReason) noexcept override {output.clear();counts.fill(0);size=0;dirty=true;clock=0;}
    void process(KernelContext& c) noexcept override {
        auto* out=c.output("notes");const auto* in=c.input("notes");size_t cursor=0;
        if(dirty||lastBypass!=c.bypassed||!in){clear(out);dirty=false;lastBypass=c.bypassed;}
        const uint64_t delay=std::max<uint64_t>(1,uint64_t(std::llround(time*rate)));
        for(int f=0;f<c.frames;++f) {
            while(in&&cursor<in->midiCount&&in->midi[cursor].frame<=f) {
                const auto e=in->midi[cursor++];send(out,e);
                if(!c.bypassed&&(e.status&0xf0)==0x90&&e.b&&feedback>0)
                    schedule(out,{clock+uint64_t(f)+delay,uint8_t(0x90|(e.status&15)),uint8_t(std::clamp(int(e.a)+pitch,0,127)),uint8_t(std::clamp(int(std::lround(e.b*feedback)),1,127)),1});
            }
            while(size&&queue[0].due<=clock+uint64_t(f)) {
                const auto e=queue[0];for(size_t i=1;i<size;++i)queue[i-1]=queue[i];--size;
                send(out,{f,e.status,e.a,e.b});
                if((e.status&0xf0)==0x90&&e.b) {
                    schedule(out,{e.due+uint64_t(.15*rate),uint8_t(0x80|(e.status&15)),e.a,0,e.step});
                    if(e.step<repeats&&feedback>0)schedule(out,{e.due+delay,e.status,uint8_t(std::clamp(int(e.a)+pitch,0,127)),uint8_t(std::clamp(int(std::lround(e.b*feedback)),1,127)),e.step+1});
                }
            }
        }
        clock+=uint64_t(std::max(0,c.frames));if(out&&out->overflow)dirty=true;
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
