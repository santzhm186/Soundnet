#pragma once
#include "../../platform/AudioProcessor.hpp"
#include "Parameters.hpp"
namespace tri::nodes::reverb {
class ReverbProcessor final : public AudioProcessor {
    static constexpr int combCount=8,allpassCount=4;
    static constexpr int combTuning[combCount]={1116,1188,1277,1356,1422,1491,1557,1617};
    static constexpr int allpassTuning[allpassCount]={556,441,341,225};
    static constexpr int stereoSpread=23;
    std::atomic<float> mix_{.25f},size_{.55f},damping_{.4f},width_{.9f},preDelay_{0};
    double rate_=48000;
    std::array<std::vector<float>,combCount> combL_,combR_;
    std::array<std::vector<float>,allpassCount> allpassL_,allpassR_;
    std::array<size_t,combCount> combIndexL_{},combIndexR_{};
    std::array<size_t,allpassCount> allpassIndexL_{},allpassIndexR_{};
    std::array<float,combCount> combStoreL_{},combStoreR_{};
    std::vector<float> preL_,preR_;
    size_t preWrite_=0;
    static float clamp(float v,float lo,float hi) {return std::max(lo,std::min(hi,v));}
    static void clear(std::array<std::vector<float>,combCount>& buffers,
                      std::array<size_t,combCount>& indices,std::array<float,combCount>& stores) {
        for(int i=0;i<combCount;++i){std::fill(buffers[i].begin(),buffers[i].end(),0.0f);indices[i]=0;stores[i]=0;}
    }
    static void clear(std::array<std::vector<float>,allpassCount>& buffers,std::array<size_t,allpassCount>& indices) {
        for(int i=0;i<allpassCount;++i){std::fill(buffers[i].begin(),buffers[i].end(),0.0f);indices[i]=0;}
    }
public:
    void prepare(double sampleRate) override {
        rate_=sampleRate>0?sampleRate:48000;
        const double scale=rate_/44100.0;
        for(int i=0;i<combCount;++i) {
            size_t left=std::max<size_t>(1,size_t(combTuning[i]*scale));
            size_t right=std::max<size_t>(1,size_t((combTuning[i]+stereoSpread)*scale));
            combL_[i].assign(left,0.0f);combR_[i].assign(right,0.0f);
        }
        for(int i=0;i<allpassCount;++i) {
            size_t left=std::max<size_t>(1,size_t(allpassTuning[i]*scale));
            size_t right=std::max<size_t>(1,size_t((allpassTuning[i]+stereoSpread)*scale));
            allpassL_[i].assign(left,0.0f);allpassR_[i].assign(right,0.0f);
        }
        preL_.assign(size_t(rate_*.25)+4,0);preR_.assign(preL_.size(),0);
        reset();
    }
    void reset() override {
        clear(combL_,combIndexL_,combStoreL_);clear(combR_,combIndexR_,combStoreR_);
        clear(allpassL_,allpassIndexL_);clear(allpassR_,allpassIndexR_);
        std::fill(preL_.begin(),preL_.end(),0);std::fill(preR_.begin(),preR_.end(),0);preWrite_=0;
    }
    void set(int param,float value) override {
        if(param==ReverbMix)mix_.store(clamp(value,0,1));
        else if(param==ReverbSize)size_.store(clamp(value,0,1));
        else if(param==ReverbDamping)damping_.store(clamp(value,0,1));
        else if(param==ReverbWidth)width_.store(clamp(value,0,1));
        else if(param==ReverbPreDelay)preDelay_.store(clamp(value,0,200));
    }
    void render(const float* inL,const float* inR,float* outL,float* outR,int frames) override {
        if(frames<=0)return;
        if(preL_.empty()||preL_.size()!=preR_.size()) {
            std::fill(outL,outL+frames,0);std::fill(outR,outR+frames,0);return;
        }
        const float mix=mix_.load(),feedback=.7f+size_.load()*.28f,
                    damp=damping_.load()*.4f,width=width_.load();
        const float wet1=width*.5f+.5f,wet2=(1.0f-width)*.5f;
        const size_t preSize=preL_.size();
        const size_t preSamples=std::min(preSize-1,size_t(preDelay_.load()*rate_/1000.0));
        for(int i=0;i<frames;++i) {
            const float dryL=inL?inL[i]:0.0f,dryR=inR?inR[i]:0.0f;
            float delayL=dryL,delayR=dryR;
            if(preSamples) {
                size_t read=preWrite_+preSize-preSamples;if(read>=preSize)read-=preSize;
                delayL=preL_[read];delayR=preR_[read];
            }
            preL_[preWrite_]=dryL;preR_[preWrite_]=dryR;
            preWrite_=(preWrite_+1)%preSize;
            const float input=(delayL+delayR)*.015f;
            float left=0,right=0;
            for(int c=0;c<combCount;++c) {
                auto& bufferL=combL_[c];size_t& indexL=combIndexL_[c];
                float output=bufferL[indexL];
                combStoreL_[c]=output*(1.0f-damp)+combStoreL_[c]*damp;
                bufferL[indexL]=input+combStoreL_[c]*feedback;
                if(++indexL>=bufferL.size())indexL=0;
                left+=output;
                auto& bufferR=combR_[c];size_t& indexR=combIndexR_[c];
                output=bufferR[indexR];
                combStoreR_[c]=output*(1.0f-damp)+combStoreR_[c]*damp;
                bufferR[indexR]=input+combStoreR_[c]*feedback;
                if(++indexR>=bufferR.size())indexR=0;
                right+=output;
            }
            for(int a=0;a<allpassCount;++a) {
                auto& bufferL=allpassL_[a];size_t& indexL=allpassIndexL_[a];
                float buffered=bufferL[indexL];
                float output=buffered-left;
                bufferL[indexL]=left+buffered*.5f;
                if(++indexL>=bufferL.size())indexL=0;
                left=output;
                auto& bufferR=allpassR_[a];size_t& indexR=allpassIndexR_[a];
                buffered=bufferR[indexR];
                output=buffered-right;
                bufferR[indexR]=right+buffered*.5f;
                if(++indexR>=bufferR.size())indexR=0;
                right=output;
            }
            float yL=dryL*(1.0f-mix)+(left*wet1+right*wet2)*mix;
            float yR=dryR*(1.0f-mix)+(right*wet1+left*wet2)*mix;
            outL[i]=std::isfinite(yL)?yL:0.0f;
            outR[i]=std::isfinite(yR)?yR:0.0f;
        }
    }
};
}
