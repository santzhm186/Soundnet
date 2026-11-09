#pragma once
#include "../../platform/AudioProcessor.hpp"
namespace tri {
class ChorusProcessor final : public AudioProcessor {
    std::atomic<float> rate_{.6f},depth_{.35f},mix_{.35f},delay_{12},feedback_{0};
    double sampleRate_=48000,phase_=0;
    std::vector<float> bufferL_,bufferR_;
    size_t write_=0;
    static float clamp(float v,float lo,float hi) {return std::max(lo,std::min(hi,v));}
    float read(const std::vector<float>& buffer,double delay) const {
        const double size=double(buffer.size());
        double position=double(write_)-delay;
        while(position<0)position+=size;
        while(position>=size)position-=size;
        const size_t i0=size_t(position),i1=(i0+1)%buffer.size();
        const float fraction=float(position-double(i0));
        return buffer[i0]*(1.0f-fraction)+buffer[i1]*fraction;
    }
public:
    void prepare(double sampleRate) override {
        sampleRate_=sampleRate>0?sampleRate:48000;
        const size_t size=size_t(sampleRate_*0.06)+4;
        bufferL_.assign(size,0);bufferR_.assign(size,0);
        reset();
    }
    void reset() override {
        std::fill(bufferL_.begin(),bufferL_.end(),0);
        std::fill(bufferR_.begin(),bufferR_.end(),0);
        write_=0;phase_=0;
    }
    void set(int param,float value) override {
        if(param==ChorusRate)rate_.store(clamp(value,.05f,8));
        else if(param==ChorusDepth)depth_.store(clamp(value,0,1));
        else if(param==ChorusMix)mix_.store(clamp(value,0,1));
        else if(param==ChorusDelay)delay_.store(clamp(value,1,30));
        else if(param==ChorusFeedback)feedback_.store(clamp(value,0,.85f));
    }
    void render(const float* inL,const float* inR,float* outL,float* outR,int frames) override {
        if(bufferL_.empty()||bufferL_.size()!=bufferR_.size()) {
            std::fill(outL,outL+frames,0);std::fill(outR,outR+frames,0);return;
        }
        const double base=double(delay_.load())*sampleRate_/1000.0;
        const double modulation=double(depth_.load())*0.008*sampleRate_;
        const double mix=double(mix_.load());
        const double feedback=double(feedback_.load());
        const double increment=2.0*3.14159265358979323846*double(rate_.load())/sampleRate_;
        const double limit=double(bufferL_.size())-2.0;
        for(int i=0;i<frames;++i) {
            const float l=inL?inL[i]:0.0f,r=inR?inR[i]:0.0f;
            const double leftDelay=std::min(limit,base+modulation*(0.5+0.5*std::sin(phase_)));
            const double rightDelay=std::min(limit,base+modulation*(0.5+0.5*std::sin(phase_+1.57079632679489661923)));
            const float wetL=read(bufferL_,leftDelay),wetR=read(bufferR_,rightDelay);
            bufferL_[write_]=clamp(l+wetL*float(feedback),-4,4);
            bufferR_[write_]=clamp(r+wetR*float(feedback),-4,4);
            write_=(write_+1)%bufferL_.size();
            outL[i]=float(l*(1.0-mix)+wetL*mix);
            outR[i]=float(r*(1.0-mix)+wetR*mix);
            phase_+=increment;
            if(phase_>=6.28318530717958647692)phase_-=6.28318530717958647692;
        }
    }
};
}
