#pragma once
#include "../../platform/AudioProcessor.hpp"
#include "Parameters.hpp"
namespace tri::nodes::phaser {
class PhaserProcessor final : public AudioProcessor {
    std::atomic<float> rate_{.5f},depth_{.7f},feedback_{.3f},mix_{.5f};
    double sampleRate_=48000,phase_=0;
    std::array<float,4> stateL_{},stateR_{};
    float lastL_=0,lastR_=0;
    static float clamp(float v,float lo,float hi) {return std::max(lo,std::min(hi,v));}
    static float allpass(float x,float a,float& state) {
        const float y=-a*x+state;
        state=x+a*y;
        return y;
    }
public:
    void prepare(double sampleRate) override {
        sampleRate_=sampleRate>0?sampleRate:48000;
        reset();
    }
    void reset() override {phase_=0;stateL_.fill(0);stateR_.fill(0);lastL_=lastR_=0;}
    void set(int param,float value) override {
        if(param==PhaserRate)rate_.store(clamp(value,.05f,8));
        else if(param==PhaserDepth)depth_.store(clamp(value,0,1));
        else if(param==PhaserFeedback)feedback_.store(clamp(value,0,.9f));
        else if(param==PhaserMix)mix_.store(clamp(value,0,1));
    }
    void render(const float* inL,const float* inR,float* outL,float* outR,int frames) override {
        const double step=double(rate_.load())/sampleRate_;
        const float depth=depth_.load(),feedback=feedback_.load(),mix=mix_.load();
        for(int i=0;i<frames;++i) {
            const float l=inL?inL[i]:0.0f,r=inR?inR[i]:0.0f;
            const float modL=float(std::sin(phase_*6.28318530717958647692));
            const float modR=float(std::sin((phase_+.25)*6.28318530717958647692));
            const float cutoffL=clamp(300.f+1700.f*(.5f+.5f*depth*modL),50.f,float(sampleRate_*.45));
            const float cutoffR=clamp(300.f+1700.f*(.5f+.5f*depth*modR),50.f,float(sampleRate_*.45));
            const float aL=float((1-std::tan(3.14159265358979323846*cutoffL/sampleRate_))/(1+std::tan(3.14159265358979323846*cutoffL/sampleRate_)));
            const float aR=float((1-std::tan(3.14159265358979323846*cutoffR/sampleRate_))/(1+std::tan(3.14159265358979323846*cutoffR/sampleRate_)));
            float xL=l+feedback*lastL_,xR=r+feedback*lastR_;
            for(int stage=0;stage<4;++stage) {
                xL=allpass(xL,aL,stateL_[stage]);
                xR=allpass(xR,aR,stateR_[stage]);
            }
            lastL_=xL;lastR_=xR;
            outL[i]=l*(1-mix)+xL*mix;
            outR[i]=r*(1-mix)+xR*mix;
            phase_+=step;
            if(phase_>=1.0)phase_-=std::floor(phase_);
        }
    }
};
}
