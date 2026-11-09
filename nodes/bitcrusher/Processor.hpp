#pragma once
#include "../../platform/AudioProcessor.hpp"
namespace tri {
class BitcrusherProcessor final : public AudioProcessor {
    std::atomic<float> bits_{8},rate_{8000},mix_{1};
    double sampleRate_=48000,phase_=0;
    float holdL_=0,holdR_=0;
    static float clamp(float v,float lo,float hi) {return std::max(lo,std::min(hi,v));}
public:
    void prepare(double sampleRate) override {
        sampleRate_=sampleRate>0?sampleRate:48000;
        reset();
    }
    void reset() override {phase_=0;holdL_=holdR_=0;}
    void set(int param,float value) override {
        if(param==CrushBits)bits_.store(clamp(value,1,16));
        else if(param==CrushRate)rate_.store(clamp(value,500,48000));
        else if(param==CrushMix)mix_.store(clamp(value,0,1));
    }
    void render(const float* inL,const float* inR,float* outL,float* outR,int frames) override {
        const double step=double(rate_.load())/sampleRate_;
        const float mix=mix_.load();
        const float levels=float((1<<int(std::lround(bits_.load())))-1);
        for(int i=0;i<frames;++i) {
            const float l=inL?inL[i]:0.0f,r=inR?inR[i]:0.0f;
            phase_+=step;
            if(phase_>=1.0) {
                phase_-=std::floor(phase_);
                holdL_=std::round(clamp(l,-1,1)*levels)/levels;
                holdR_=std::round(clamp(r,-1,1)*levels)/levels;
            }
            outL[i]=l*(1-mix)+holdL_*mix;outR[i]=r*(1-mix)+holdR_*mix;
        }
    }
};
}
