#pragma once
#include "../../platform/NodeKernel.hpp"
#include "Parameters.hpp"
#include <algorithm>
#include <cmath>
namespace tri::nodes::audio_clip {
class Kernel final : public NodeKernel {
    std::shared_ptr<const AudioSample> audio;
    double rate=48000,position=0,step=1,fade=.01,xfade=.005;
    size_t first=0,last=0;float gain=.9f,envelope=0;
    bool loop=false,reverse=false,playing=false,stopping=false;
    void trigger(bool start) noexcept {
        if(start&&audio&&audio->valid()&&last>first) {position=reverse?double(last-1):double(first);envelope=fade>0?0:1;playing=true;stopping=false;}
        else if(!start&&playing){if(fade<=0)playing=false;else stopping=true;}
    }
    void render(float& left,float& right) noexcept {
        if(!playing||!audio||!audio->valid()||last<=first)return;
        const bool looping=loop&&last>first+1;const double length=double(last-first);
        if(position<double(first)||position>=double(last)) {
            if(looping) {double local=std::fmod(position-double(first),length);if(local<0)local+=length;position=double(first)+local;}
            else if(stopping&&envelope>0)position=std::clamp(position,double(first),double(last-1));
            else {playing=false;return;}
        }
        const size_t i0=size_t(position),i1=i0+1<last?i0+1:(looping?first:last-1);
        const float fraction=float(position-double(i0));
        auto sample=[&](size_t channel){const float a=audio->samples[i0*size_t(audio->channels)+channel];return a+(audio->samples[i1*size_t(audio->channels)+channel]-a)*fraction;};
        const double crossfade=looping?std::min(length*.5,xfade*rate):0;
        float loopGain=1;
        if(crossfade>0) {
            const double since=reverse?double(last)-position:position-double(first),remaining=reverse?position-double(first):double(last)-position;
            loopGain=float(std::min(1.0,std::max(0.0,since)/crossfade)*std::min(1.0,std::max(0.0,remaining)/crossfade));
        }
        const float amplitude=envelope*loopGain*gain;left=sample(0)*amplitude;right=(audio->channels>1?sample(1)*amplitude:left);
        const float increment=float(1/std::max(1.0,fade*rate));
        if(stopping){envelope-=increment;if(envelope<=0){envelope=0;playing=false;return;}}
        else envelope=std::min(1.f,envelope+increment);
        position+=step*(reverse?-1:1);
        if(!looping&&(position<double(first)||position>=double(last))) {stopping=true;if(fade<=0)playing=false;}
    }
public:
    void prepare(double sampleRate,int) override {rate=sampleRate>0?sampleRate:48000;reset(KernelResetReason::Reconfigure);}
    void configure(const KernelConfig& config) override {
        audio=config.audio;gain=config.value(AudioClipGain,.9f);loop=config.value(AudioClipLoop,0)>=.5f;reverse=config.value(AudioClipReverse,0)>=.5f;
        fade=config.value(AudioClipFade,.01f);xfade=config.value(AudioClipXfade,5)*.001;
        const size_t frames=audio&&audio->valid()?audio->frames():0;
        const double start=std::clamp(double(config.value(AudioClipStart,0)),0.0,1.0),end=std::clamp(double(config.value(AudioClipEnd,1)),0.0,1.0);
        first=frames?size_t(start*double(frames-1)):0;last=frames?std::max(first+1,size_t(end*double(frames))):0;
        step=audio?audio->rate/rate*std::exp2(config.value(AudioClipPitch,0)/12.0):1;
        reset(KernelResetReason::Reconfigure);
    }
    void reset(KernelResetReason) noexcept override {playing=false;stopping=false;envelope=0;position=0;}
    void process(KernelContext& context) noexcept override {
        auto* output=context.output("audio");const auto* input=context.input("trigger");size_t cursor=0;
        if(context.bypassed)reset(KernelResetReason::Stop);
        for(int frame=0;frame<context.frames;++frame) {
            if(!context.bypassed)while(input&&input->triggers&&cursor<input->triggerCount&&input->triggers[cursor].frame<=frame)trigger(input->triggers[cursor++].start);
            float left=0,right=0;if(!context.bypassed)render(left,right);
            if(output){if(output->left)output->left[frame]=left;if(output->right)output->right[frame]=right;}
        }
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
