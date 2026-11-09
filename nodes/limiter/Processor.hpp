#pragma once
#include "../../platform/AudioProcessor.hpp"
namespace tri {
class LimiterProcessor final : public AudioProcessor {
    std::atomic<float> threshold_{-6},release_{.12f},ceiling_{-0.3f},gain_{1};
    double rate_=48000;
    float envelope_=1;
    static float clamp(float v,float lo,float hi) {return std::max(lo,std::min(hi,v));}
public:
    void prepare(double sampleRate) override {rate_=sampleRate>0?sampleRate:48000;reset();}
    void reset() override {envelope_=1;}
    void set(int param,float value) override {
        if(param==LimiterThreshold)threshold_.store(clamp(value,-60,0));
        else if(param==LimiterRelease)release_.store(clamp(value,.01f,2));
        else if(param==LimiterCeiling)ceiling_.store(clamp(value,-12,0));
        else if(param==LimiterGain)gain_.store(clamp(value,0,2));
    }
    void render(const float* inL,const float* inR,float* outL,float* outR,int frames) override {
        const float threshold=std::pow(10.0f,threshold_.load()/20.0f);
        const float ceiling=std::pow(10.0f,ceiling_.load()/20.0f);
        const float inputGain=gain_.load();
        const float attack=std::exp(-1.0f/float(std::max(1.0,0.001*rate_)));
        const float release=std::exp(-1.0f/float(std::max(1.0,release_.load()*rate_)));
        float envelope=envelope_;
        for(int i=0;i<frames;++i) {
            const float l=(inL?inL[i]:0.0f)*inputGain,r=(inR?inR[i]:0.0f)*inputGain;
            const float peak=std::max(std::fabs(l),std::fabs(r));
            float target=1.0f;
            if(peak>1.0e-9f) {
                if(peak>threshold)target=std::min(target,threshold/peak);
                target=std::min(target,ceiling/peak);
            }
            const float coefficient=target<envelope?attack:release;
            envelope=coefficient*envelope+(1.0f-coefficient)*target;
            outL[i]=clamp(l*envelope,-ceiling,ceiling);
            outR[i]=clamp(r*envelope,-ceiling,ceiling);
        }
        envelope_=envelope;
    }
};
}
