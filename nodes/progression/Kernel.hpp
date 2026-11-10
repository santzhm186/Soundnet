#pragma once
#include "Parameters.hpp"
#include "../../platform/runtime/MidiNoteState.hpp"
#include "../../platform/music/Harmony.hpp"
#include <algorithm>
#include <cmath>
namespace tri::nodes::progression {
class Kernel final : public NodeKernel {
    MidiNoteState output;std::array<int,8> roots{60,69,62,67,60,60,60,60},qualities{3,4,4,2,0,0,0,0};
    double rate=48000,bpm=112,remaining=0;int length=4,bars=1,index=0;bool running=false,configuredRun=false,pending=true,seekPending=false;
    void chord(KernelOutput* out,int frame) noexcept {
        output.release(out,frame);if(!running)return;
        for(int d:chordIntervals[qualities[index]])if(d>=0&&roots[index]+d<128)
            output.emit(out,{frame,0x90,uint8_t(roots[index]+d),100});
    }
public:
    void prepare(double r,int) override {rate=r>0?r:48000;output.clear();remaining=0;index=0;pending=true;}
    void configure(const KernelConfig& c) override {
        const int r[]={ProgressRoot1,ProgressRoot2,ProgressRoot3,ProgressRoot4,ProgressRoot5,ProgressRoot6,ProgressRoot7,ProgressRoot8};
        const int q[]={ProgressQuality1,ProgressQuality2,ProgressQuality3,ProgressQuality4,ProgressQuality5,ProgressQuality6,ProgressQuality7,ProgressQuality8};
        const int rd[]={60,69,62,67,60,60,60,60},qd[]={3,4,4,2,0,0,0,0};
        for(int i=0;i<8;++i){roots[i]=std::clamp(int(c.value(r[i],rd[i])),24,96);qualities[i]=std::clamp(int(c.value(q[i],qd[i])),0,7);}
        length=std::clamp(int(c.value(ProgressLength,4)),1,8);bars=std::clamp(int(c.value(ProgressBars,1)),1,8);
        bpm=std::clamp(double(c.value(ClockBPM,112)),40.0,240.0);running=configuredRun=c.value(ProgressRun,0)>.5f;
        index=0;remaining=4*bars;pending=true;seekPending=running;
    }
    void reset(KernelResetReason reason) noexcept override {output.clear();running=reason==KernelResetReason::Seek&&configuredRun;index=0;remaining=0;pending=true;seekPending=reason==KernelResetReason::Seek;}
    void process(KernelContext& c) noexcept override {
        auto* out=c.output("notes");const auto* in=c.input("trigger");size_t cursor=0;
        if(seekPending&&running) {
            const double beat=double(c.framePosition)*bpm/(60*rate),span=4.0*bars;
            index=int(std::fmod(std::floor(beat/span),double(length)));remaining=span-std::fmod(beat,span);seekPending=false;
        }
        if(pending){chord(out,0);pending=false;}
        for(int f=0;f<c.frames;++f) {
            while(in&&cursor<in->triggerCount&&in->triggers[cursor].frame<=f) {
                running=in->triggers[cursor++].start;index=0;remaining=4*bars;chord(out,f);
            }
            if(running&&remaining<=1e-9){index=(index+1)%length;remaining+=4*bars;chord(out,f);}
            if(running)remaining-=bpm/(60*rate);
        }
        if(out&&out->overflow)pending=true;
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
