#pragma once
#include "../../platform/AudioProcessor.hpp"
#include "Parameters.hpp"
namespace tri::nodes::tremolo {
class TremoloProcessor final : public AudioProcessor {
    std::atomic<float> rate_{4},depth_{.5f},mix_{1},stereo_{0};
    double sampleRate_=48000,phase_=0;
    static float clamp(float v,float lo,float hi) {return std::max(lo,std::min(hi,v));}
public:
    void prepare(double sampleRate) override {sampleRate_=sampleRate>0?sampleRate:48000;reset();}
    void reset() override {phase_=0;}
    void set(int param,float value) override {
        if(param==TremoloRate)rate_.store(clamp(value,.1f,20));
        else if(param==TremoloDepth)depth_.store(clamp(value,0,1));
        else if(param==TremoloMix)mix_.store(clamp(value,0,1));
        else if(param==TremoloStereo)stereo_.store(clamp(value,0,1));
    }
    void render(const float* inL,const float* inR,float* outL,float* outR,int frames) override {
        const double increment=2.0*3.14159265358979323846*double(rate_.load())/sampleRate_;
        const double phaseOffset=double(stereo_.load())*3.14159265358979323846;
        const float depth=depth_.load(),mix=mix_.load();
        for(int i=0;i<frames;++i) {
            const float l=inL?inL[i]:0.0f,r=inR?inR[i]:0.0f;
            const float lfoL=float(.5+.5*std::sin(phase_));
            const float lfoR=float(.5+.5*std::sin(phase_+phaseOffset));
            const float wetL=l*(1-depth*(1-lfoL)),wetR=r*(1-depth*(1-lfoR));
            outL[i]=l*(1-mix)+wetL*mix;outR[i]=r*(1-mix)+wetR*mix;
            phase_+=increment;
            if(phase_>6.28318530717958647692)phase_-=6.28318530717958647692;
        }
    }
};
}
