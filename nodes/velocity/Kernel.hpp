#pragma once
#include "Parameters.hpp"
#include "../../platform/runtime/MidiNoteState.hpp"
#include <algorithm>
#include <cmath>
namespace tri::nodes::velocity {
class Kernel final : public NodeKernel {
    MidiNoteState output;double rate=48000,bpm=112,period=4;int low=48,high=112,shape=0;bool clearPending=false;
public:
    void prepare(double r,int) override {rate=r>0?r:48000;output.clear();}
    void configure(const KernelConfig& c) override {
        bpm=std::clamp(double(c.value(ClockBPM,112)),40.0,240.0);period=std::clamp(double(c.value(VelocityRate,4)),.25,16.0);
        low=std::clamp(int(c.value(VelocityMin,48)),1,127);high=std::clamp(int(c.value(VelocityMax,112)),1,127);
        if(low>high)std::swap(low,high);shape=std::clamp(int(c.value(VelocityShape,0)),0,2);
    }
    void reset(KernelResetReason) noexcept override {output.clear();clearPending=false;}
    void process(KernelContext& c) noexcept override {
        auto* out=c.output("notes");const auto* in=c.input("notes");
        if(clearPending||!in){output.release(out);clearPending=false;}
        if(!in)return;
        for(size_t i=0;i<in->midiCount;++i) {
            auto e=in->midi[i];if((e.status&0xf0)==0x90&&e.b&&!c.bypassed) {
                const double beat=(double(c.framePosition)+std::max(0,e.frame))*bpm/(60*rate);
                const double phase=std::fmod(beat/period,1.0);
                const double modulation=shape==0?.5-.5*std::cos(phase*6.283185307179586):shape==1?phase:(phase<.5?1:0);
                e.b=uint8_t(std::clamp(int(std::lround(low+(high-low)*modulation)),1,127));
            }
            output.emit(out,e);
        }
        if(out&&out->overflow)clearPending=true;
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
