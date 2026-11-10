#pragma once
#include "Parameters.hpp"
#include "../../platform/runtime/MidiNoteState.hpp"
#include <algorithm>
#include <cmath>
namespace tri::nodes::arp {
class Kernel final : public NodeKernel {
    MidiNoteState input,output;double rate=48000,bpm=112,gate=.6,untilStep=0,untilOff=0;
    int pattern=0,repeat=1,step=0,last=-1,hits=0,current=-1;bool enabled=false,dirty=true,seekPending=false;
    static constexpr uint16_t patterns[]={0x5555,0x2449,0x4444,0x6565,0xffff};
    void silence(KernelOutput* out,int frame) noexcept {output.release(out,frame);current=-1;}
    void restart(KernelOutput* out,int frame) noexcept {silence(out,frame);step=0;last=-1;hits=0;untilStep=untilOff=0;}
public:
    void prepare(double r,int) override {rate=r>0?r:48000;input.clear();output.clear();dirty=true;}
    void configure(const KernelConfig& c) override {
        enabled=c.value(ArpOn,0)>.5f;pattern=std::clamp(int(c.value(Pattern,0)),0,4);repeat=std::clamp(int(c.value(ArpRepeat,1)),1,8);
        bpm=std::clamp(double(c.value(ClockBPM,112)),40.0,240.0);gate=std::clamp(double(c.value(Gate,.6f)),.08,.98);dirty=true;seekPending=enabled;
    }
    void reset(KernelResetReason reason) noexcept override {input.clear();output.clear();dirty=true;seekPending=reason==KernelResetReason::Seek;}
    void process(KernelContext& c) noexcept override {
        auto* out=c.output("notes");const auto* notes=c.input("notes");const auto* triggers=c.input("trigger");
        if(dirty){restart(out,0);dirty=false;}
        if(!notes){input.clear();silence(out,0);}
        size_t ni=0,ti=0;const double sixteenth=rate*60/(bpm*4);
        if(seekPending&&enabled){const double next=std::ceil(double(c.framePosition)/sixteenth-1e-9);step=int(std::fmod(next,16.0));untilStep=std::max(0.0,next*sixteenth-double(c.framePosition));seekPending=false;}
        for(int f=0;f<c.frames;++f) {
            while(triggers&&ti<triggers->triggerCount&&triggers->triggers[ti].frame<=f) {
                enabled=triggers->triggers[ti++].start;restart(out,f);
                if(!enabled)for(int k=0;k<2048;++k)if(input.notes[k].velocity)output.emit(out,{f,uint8_t(0x90|(k%16)),uint8_t(k/16),input.notes[k].velocity});
            }
            while(notes&&ni<notes->midiCount&&notes->midi[ni].frame<=f) {
                const auto e=notes->midi[ni++];input.observe(e);const int kind=e.status&0xf0;
                if(!enabled||c.bypassed|| (kind!=0x80&&kind!=0x90))output.emit(out,e);
                if(enabled&&current==int(e.a)*16+(e.status&15)&&(kind==0x80||(kind==0x90&&!e.b))){silence(out,f);}
            }
            if(!enabled||c.bypassed)continue;
            if(current>=0&&untilOff<=0)silence(out,f);
            if(untilStep<=1e-9) {
                if((patterns[pattern]>>step)&1) {
                    silence(out,f);const bool repeating=hits>0&&hits<repeat;
                    for(int i=repeating?0:1;i<(repeating?2048:2049);++i) {
                        const int k=(last+i)&2047;if(!input.notes[k].velocity)continue;
                        hits=repeating&&k==last?hits+1:1;last=current=k;
                        const uint8_t velocity=uint8_t(std::clamp(int(std::lround(input.notes[k].velocity*(step%4==0?1.0:.85))),1,127));
                        output.emit(out,{f,uint8_t(0x90|(k%16)),uint8_t(k/16),velocity});
                        int steps=1;while(steps<2&&!((patterns[pattern]>>((step+steps)&15))&1))++steps;
                        untilOff=sixteenth*steps*gate;break;
                    }
                }
                step=(step+1)&15;untilStep+=sixteenth;
            }
            untilStep-=1;untilOff-=1;
        }
        if(out&&out->overflow)dirty=true;
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
