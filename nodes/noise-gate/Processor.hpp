#pragma once
#include "../../platform/AudioProcessor.hpp"
#include "Parameters.hpp"
namespace tri::nodes::noise_gate {
class GateProcessor final : public AudioProcessor {
    std::atomic<float> threshold_{-40},attack_{.005f},release_{.15f},range_{.8f};
    double rate_=48000;
    float gain_=1;
    static float clamp(float v,float lo,float hi){return std::max(lo,std::min(hi,v));}
public:
    void prepare(double rate) override {rate_=rate>0?rate:48000;reset();}
    void reset() override {gain_=1;}
    void set(int param,float value) override {
        if(param==GateThreshold)threshold_.store(value);
        else if(param==GateAttack)attack_.store(value);
        else if(param==GateRelease)release_.store(value);
        else if(param==GateRange)range_.store(value);
    }
    void render(const float* inL,const float* inR,float* outL,float* outR,int frames) override {
        const float threshold=clamp(threshold_.load(),-80.f,0.f);
        const float attack=clamp(attack_.load(),.001f,1.f);
        const float release=clamp(release_.load(),.01f,2.f);
        const float floorGain=1-clamp(range_.load(),0.f,1.f);
        const float attackCoef=std::exp(-1.f/float(attack*rate_));
        const float releaseCoef=std::exp(-1.f/float(release*rate_));
        for(int i=0;i<frames;++i) {
            const float peak=std::max(std::fabs(inL?inL[i]:0.f),std::fabs(inR?inR[i]:0.f));
            const float db=20.f*std::log10(peak+1e-9f);
            const float target=db>=threshold?1.f:floorGain;
            const float coef=target>gain_?attackCoef:releaseCoef;
            gain_=target+(gain_-target)*coef;
            outL[i]=(inL?inL[i]:0.f)*gain_;
            outR[i]=(inR?inR[i]:0.f)*gain_;
        }
    }
};
}
