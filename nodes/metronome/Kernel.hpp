#pragma once
#include "Parameters.hpp"
#include "../../platform/NodeKernel.hpp"
#include <algorithm>
#include <cmath>
namespace tri::nodes::metronome {
class Kernel final : public NodeKernel {
    double rate=48000,phase=0,oscillator=0,bpm=120;float envelope=0,level=.5f,accent=1;int beats=4,beat=0;bool running=false;
public:
    void prepare(double r,int) override {rate=r>0?r:48000;reset(KernelResetReason::Stop);}
    void configure(const KernelConfig& c) override {
        bpm=std::clamp(double(c.value(MetronomeBPM,120)),40.0,240.0);
        level=std::clamp(c.value(MetronomeLevel,.5f),0.f,1.f);accent=std::clamp(c.value(MetronomeAccent,1),0.f,1.f);
        beats=std::clamp(int(c.value(MetronomeBeats,4)),1,16);
    }
    void reset(KernelResetReason) noexcept override {phase=oscillator=0;envelope=0;beat=0;running=false;}
    void process(KernelContext& c) noexcept override {
        const auto* in=c.input("trigger");auto* out=c.output("audio");size_t cursor=0;
        const double step=bpm/(60*rate);const float decay=std::exp(-1.f/float(.035*rate));
        for(int f=0;f<c.frames;++f) {
            while(in&&cursor<in->triggerCount&&in->triggers[cursor].frame<=f) {
                running=in->triggers[cursor++].start;
                if(running){phase=0;beat=0;envelope=1;oscillator=0;}else envelope=0;
            }
            if(c.bypassed){envelope=0;}else if(running&&phase>=1){phase-=1;beat=(beat+1)%beats;envelope=1;oscillator=0;}
            const float v=c.bypassed?0:std::sin(float(6.283185307179586*oscillator))*envelope*level*(beat==0?accent:1);
            if(out){if(out->left)out->left[f]=v;if(out->right)out->right[f]=v;}
            envelope*=decay;oscillator+=1800/rate;oscillator-=std::floor(oscillator);if(running)phase+=step;
        }
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
