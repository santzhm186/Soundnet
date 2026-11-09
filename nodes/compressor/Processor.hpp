#pragma once
#include "../../platform/AudioProcessor.hpp"
namespace tri {
class CompressorProcessor final : public AudioProcessor {
    std::atomic<float> threshold_{-18},ratio_{4},attack_{.01f},release_{.15f},makeup_{1},mix_{1};
    double rate_=48000;
    float gain_=1;
    const float* sideL_=nullptr;const float* sideR_=nullptr;
    static float clamp(float v,float lo,float hi) {return std::max(lo,std::min(hi,v));}
public:
    void prepare(double sampleRate) override {rate_=sampleRate>0?sampleRate:48000;reset();}
    void reset() override {gain_=1;sideL_=sideR_=nullptr;}
    void setSidechain(const float* l,const float* r,int) override {sideL_=l;sideR_=r;}
    void set(int param,float value) override {
        if(param==CompThreshold)threshold_.store(clamp(value,-60,0));
        else if(param==CompRatio)ratio_.store(clamp(value,1,20));
        else if(param==CompAttack)attack_.store(clamp(value,.001f,1));
        else if(param==CompRelease)release_.store(clamp(value,.01f,2));
        else if(param==CompMakeup)makeup_.store(clamp(value,0,2));
        else if(param==CompMix)mix_.store(clamp(value,0,1));
    }
    void render(const float* inL,const float* inR,float* outL,float* outR,int frames) override {
        const float threshold=std::pow(10.0f,threshold_.load()/20.0f);
        const float ratio=ratio_.load(),makeup=makeup_.load(),mix=mix_.load();
        const float attack=std::exp(-1.0f/float(std::max(1.0,attack_.load()*rate_)));
        const float release=std::exp(-1.0f/float(std::max(1.0,release_.load()*rate_)));
        float gain=gain_;
        for(int i=0;i<frames;++i) {
            const float l=inL?inL[i]:0.0f,r=inR?inR[i]:0.0f;
            const float detector=sideL_ ? std::max(std::fabs(sideL_[i]),std::fabs(sideR_?sideR_[i]:0.0f))
                                         : std::max(std::fabs(l),std::fabs(r));
            const float peak=detector;
            float target=1.0f;
            if(peak>threshold && peak>1.0e-9f)
                target=(threshold+(peak-threshold)/ratio)/peak;
            if(target<gain)gain=attack*gain+(1.0f-attack)*target;
            else gain=release*gain+(1.0f-release)*target;
            const float wetL=l*gain*makeup,wetR=r*gain*makeup;
            outL[i]=l*(1.0f-mix)+wetL*mix;
            outR[i]=r*(1.0f-mix)+wetR*mix;
        }
        gain_=gain;
    }
};
}
