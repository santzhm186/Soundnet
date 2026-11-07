#pragma once
// Realtime audio processors used by the 2.2 filter, reverb and noise nodes.
// Every parameter is atomic so the control thread can publish edits while the
// render thread is running. Buffers are allocated in prepare(), never in render.
#include "NodeRegistry.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace tri {
class AudioProcessor {
public:
    virtual ~AudioProcessor()=default;
    virtual void prepare(double sampleRate)=0;
    virtual void reset()=0;
    virtual void set(int param,float value)=0;
    virtual void render(const float* inL,const float* inR,float* outL,float* outR,int frames)=0;
};

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

class ReverbProcessor final : public AudioProcessor {
    static constexpr int combCount=8,allpassCount=4;
    static constexpr int combTuning[combCount]={1116,1188,1277,1356,1422,1491,1557,1617};
    static constexpr int allpassTuning[allpassCount]={556,441,341,225};
    static constexpr int stereoSpread=23;
    std::atomic<float> mix_{.25f},size_{.55f},damping_{.4f},width_{.9f};
    double rate_=48000;
    std::array<std::vector<float>,combCount> combL_,combR_;
    std::array<std::vector<float>,allpassCount> allpassL_,allpassR_;
    std::array<size_t,combCount> combIndexL_{},combIndexR_{};
    std::array<size_t,allpassCount> allpassIndexL_{},allpassIndexR_{};
    std::array<float,combCount> combStoreL_{},combStoreR_{};
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
        reset();
    }
    void reset() override {
        clear(combL_,combIndexL_,combStoreL_);clear(combR_,combIndexR_,combStoreR_);
        clear(allpassL_,allpassIndexL_);clear(allpassR_,allpassIndexR_);
    }
    void set(int param,float value) override {
        if(param==ReverbMix)mix_.store(clamp(value,0,1));
        else if(param==ReverbSize)size_.store(clamp(value,0,1));
        else if(param==ReverbDamping)damping_.store(clamp(value,0,1));
        else if(param==ReverbWidth)width_.store(clamp(value,0,1));
    }
    void render(const float* inL,const float* inR,float* outL,float* outR,int frames) override {
        if(frames<=0)return;
        const float mix=mix_.load(),feedback=.7f+size_.load()*.28f,
                    damp=damping_.load()*.4f,width=width_.load();
        const float wet1=width*.5f+.5f,wet2=(1.0f-width)*.5f;
        for(int i=0;i<frames;++i) {
            const float dryL=inL?inL[i]:0.0f,dryR=inR?inR[i]:0.0f;
            const float input=(dryL+dryR)*.015f;
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
            float yL=dryL*(1.0f-mix)+left*wet1+right*wet2;
            float yR=dryR*(1.0f-mix)+right*wet1+left*wet2;
            outL[i]=std::isfinite(yL)?yL:0.0f;
            outR[i]=std::isfinite(yR)?yR:0.0f;
        }
    }
};

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

class CompressorProcessor final : public AudioProcessor {
    std::atomic<float> threshold_{-18},ratio_{4},attack_{.01f},release_{.15f},makeup_{1},mix_{1};
    double rate_=48000;
    float gain_=1;
    static float clamp(float v,float lo,float hi) {return std::max(lo,std::min(hi,v));}
public:
    void prepare(double sampleRate) override {rate_=sampleRate>0?sampleRate:48000;reset();}
    void reset() override {gain_=1;}
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
            const float peak=std::max(std::fabs(l),std::fabs(r));
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

class ChorusProcessor final : public AudioProcessor {
    std::atomic<float> rate_{.6f},depth_{.35f},mix_{.35f},delay_{12};
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
    }
    void render(const float* inL,const float* inR,float* outL,float* outR,int frames) override {
        if(bufferL_.empty()||bufferL_.size()!=bufferR_.size()) {
            std::fill(outL,outL+frames,0);std::fill(outR,outR+frames,0);return;
        }
        const double base=double(delay_.load())*sampleRate_/1000.0;
        const double modulation=double(depth_.load())*0.008*sampleRate_;
        const double mix=double(mix_.load());
        const double increment=2.0*3.14159265358979323846*double(rate_.load())/sampleRate_;
        const double limit=double(bufferL_.size())-2.0;
        for(int i=0;i<frames;++i) {
            const float l=inL?inL[i]:0.0f,r=inR?inR[i]:0.0f;
            const double leftDelay=std::min(limit,base+modulation*(0.5+0.5*std::sin(phase_)));
            const double rightDelay=std::min(limit,base+modulation*(0.5+0.5*std::sin(phase_+1.57079632679489661923)));
            const float wetL=read(bufferL_,leftDelay),wetR=read(bufferR_,rightDelay);
            bufferL_[write_]=l;bufferR_[write_]=r;
            write_=(write_+1)%bufferL_.size();
            outL[i]=float(l*(1.0-mix)+wetL*mix);
            outR[i]=float(r*(1.0-mix)+wetR*mix);
            phase_+=increment;
            if(phase_>=6.28318530717958647692)phase_-=6.28318530717958647692;
        }
    }
};

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

inline std::unique_ptr<AudioProcessor> makeAudioProcessor(int type) {
    if(type==FilterNode)return std::make_unique<FilterProcessor>();
    if(type==ReverbNode)return std::make_unique<ReverbProcessor>();
    if(type==NoiseNode)return std::make_unique<NoiseProcessor>();
    if(type==CompressorNode)return std::make_unique<CompressorProcessor>();
    if(type==LimiterNode)return std::make_unique<LimiterProcessor>();
    if(type==ChorusNode)return std::make_unique<ChorusProcessor>();
    if(type==TremoloNode)return std::make_unique<TremoloProcessor>();
    if(type==BitcrusherNode)return std::make_unique<BitcrusherProcessor>();
    if(type==PhaserNode)return std::make_unique<PhaserProcessor>();
    return nullptr;
}
} // namespace tri
