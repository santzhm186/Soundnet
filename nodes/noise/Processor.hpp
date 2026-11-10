#pragma once
#include "../../platform/AudioProcessor.hpp"
#include "Parameters.hpp"
namespace tri::nodes::noise {
class NoiseProcessor final : public AudioProcessor {
    std::atomic<float> level_{.2f},type_{0};
    uint32_t seedL_=0x12345678u,seedR_=0x9e3779b9u;
    float pinkL_[3]{},pinkR_[3]{};
    static float clamp(float v,float lo,float hi) {return std::max(lo,std::min(hi,v));}
    static float white(uint32_t& state) {
        state^=state<<13;state^=state>>17;state^=state<<5;
        return float(int32_t(state))/2147483648.0f;
    }
    static float pink(float whiteValue,float* state) {
        state[0]=.99765f*state[0]+whiteValue*.0990460f;
        state[1]=.96300f*state[1]+whiteValue*.2965164f;
        state[2]=.57000f*state[2]+whiteValue*1.0526913f;
        return (state[0]+state[1]+state[2]+whiteValue*.1848f)*.25f;
    }
public:
    void prepare(double) override {reset();}
    void reset() override {
        seedL_=0x12345678u;seedR_=0x9e3779b9u;
        pinkL_[0]=pinkL_[1]=pinkL_[2]=0;
        pinkR_[0]=pinkR_[1]=pinkR_[2]=0;
    }
    void set(int param,float value) override {
        if(param==NoiseLevel)level_.store(clamp(value,0,1));
        else if(param==NoiseType)type_.store(clamp(value,0,1));
    }
    void render(const float*,const float*,float* outL,float* outR,int frames) override {
        const float level=level_.load();const bool usePink=type_.load()>=.5f;
        for(int i=0;i<frames;++i) {
            float l=white(seedL_),r=white(seedR_);
            if(usePink){l=pink(l,pinkL_);r=pink(r,pinkR_);}
            outL[i]=l*level;outR[i]=r*level;
        }
    }
};
}
