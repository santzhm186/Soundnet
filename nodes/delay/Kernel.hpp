#pragma once
#include "../../platform/NodeKernel.hpp"
#include "Parameters.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
namespace tri::nodes::delay {
class Kernel final : public NodeKernel {
    std::array<std::vector<float>,2> ring;
    size_t write=0;double rate=48000,current=14880,target=14880;
    float feedback=.32f,mix=.12f;
public:
    void prepare(double sampleRate,int) override {
        rate=sampleRate>0?sampleRate:48000;
        for(auto& channel:ring)channel.assign(size_t(rate)+4,0);
        current=target=rate*.310;write=0;
    }
    void configure(const KernelConfig& config) override {
        double ms=config.value(DelayTime,310);
        if(config.value(DelaySync,0)>=.5f) {
            constexpr double beats[]={1,.5,.25,.75};
            ms=60000.0/std::max(1.0,double(config.value(ClockBPM,120)))*beats[std::clamp(int(config.value(DelayDivision,0)),0,3)];
        }
        target=std::clamp(ms,20.0,1000.0)*rate*.001;
        feedback=std::clamp(config.value(DelayFeedback,.32f),0.f,.88f);
        mix=std::clamp(config.value(DelayMix,.12f),0.f,1.f);
    }
    void reset(KernelResetReason) noexcept override {
        for(auto& channel:ring)std::fill(channel.begin(),channel.end(),0);write=0;current=target;
    }
    void process(KernelContext& context) noexcept override {
        auto* output=context.output("audio");if(!output)return;const auto* input=context.input("audio");
        for(int frame=0;frame<context.frames;++frame) {
            current+=(target-current)/(.025*rate);
            const size_t size=ring[0].size();
            const size_t delay=size?std::min(size-1,size_t(std::max(0.0,current))):0;
            const size_t read=size?(write+size-delay)%size:0,previous=size?(read+size-1)%size:0;
            const float fraction=float(current-std::floor(current));
            for(int channel=0;channel<2;++channel) {
                const float* in=input?(channel?input->right:input->left):nullptr;
                float* out=channel?output->right:output->left;
                const float dry=in?in[frame]:0;
                if(context.bypassed||!size) {if(out)out[frame]=dry;continue;}
                const float wet=ring[channel][read]+fraction*(ring[channel][previous]-ring[channel][read]);
                const float value=dry+wet*feedback;ring[channel][write]=std::isfinite(value)&&std::abs(value)>=1e-22f?value:0;
                if(out)out[frame]=dry*(1-mix)+wet*mix;
            }
            if(size&&!context.bypassed)write=(write+1)%size;
        }
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
