#pragma once
#include "../../platform/AudioProcessor.hpp"
#include "Parameters.hpp"
namespace tri::nodes::filter {
class FilterProcessor final : public AudioProcessor {
    std::atomic<float> mode_{0},cutoff_{8000},resonance_{.9f},drive_{0};
    double rate_=48000;
    float ic1_[2]{},ic2_[2]{};
    static float clamp(float v,float lo,float hi) {return std::max(lo,std::min(hi,v));}
public:
    void prepare(double sampleRate) override {rate_=sampleRate>0?sampleRate:48000;reset();}
    void reset() override {ic1_[0]=ic1_[1]=ic2_[0]=ic2_[1]=0;}
    void set(int param,float value) override {
        if(param==FilterType)mode_.store(clamp(value,0,2));
        else if(param==FilterCutoff)cutoff_.store(clamp(value,20,20000));
        else if(param==FilterResonance)resonance_.store(clamp(value,.5f,20));
        else if(param==FilterDrive)drive_.store(clamp(value,0,1));
    }
    void render(const float* inL,const float* inR,float* outL,float* outR,int frames) override {
        if(frames<=0)return;
        const float mode=mode_.load(),fc=std::min(cutoff_.load(),float(rate_*.45)),
                    q=resonance_.load(),drive=drive_.load();
        const float g=std::tan(3.14159265358979323846f*fc/float(rate_));
        const float k=1.0f/q;
        const float a1=1.0f/(1.0f+g*(g+k)),a2=g*a1,a3=g*a2;
        const float gain=1.0f+drive*6.0f,compensation=1.0f/(1.0f+drive*2.0f);
        for(int channel=0;channel<2;++channel) {
            const float* in=channel?inR:inL;
            float* out=channel?outR:outL;
            float ic1=ic1_[channel],ic2=ic2_[channel];
            for(int i=0;i<frames;++i) {
                float x=in?in[i]:0.0f;
                if(drive>0) x=std::tanh(x*gain)*compensation;
                const float v3=x-ic2;
                const float v1=a1*ic1+a2*v3;
                const float v2=ic2+a2*ic1+a3*v3;
                ic1=2.0f*v1-ic1;
                ic2=2.0f*v2-ic2;
                float y=v2;
                if(mode>=1.5f)y=x-k*v1-v2;
                else if(mode>=.5f)y=v1;
                out[i]=std::isfinite(y)?y:0.0f;
            }
            ic1_[channel]=std::isfinite(ic1)?ic1:0.0f;
            ic2_[channel]=std::isfinite(ic2)?ic2:0.0f;
        }
    }
};
}
