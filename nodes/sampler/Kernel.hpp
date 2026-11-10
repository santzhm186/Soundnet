#pragma once
#include "../../platform/NodeKernel.hpp"
#include "Parameters.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace tri::nodes::sampler {
class Kernel final : public NodeKernel {
    struct Voice {int note=0,channel=0;double position=0,step=1;float velocity=0,envelope=0;uint64_t age=0;bool active=false,releasing=false;};
    std::shared_ptr<const AudioSample> audio;
    std::array<Voice,16> voices{};
    std::array<std::array<unsigned,128>,16> down{};
    std::array<bool,16> pedal{};
    std::array<double,16> bend{};
    double rate=48000,root=60,start=0,end=1,attack=.005,release=.12,xfade=.005;
    float gain=.8f,velocitySensitivity=1;bool loop=false,reverse=false;uint64_t serial=0;
    size_t first=0,last=0;
    void releaseNote(int channel,int note) noexcept {for(auto& voice:voices)if(voice.active&&voice.channel==channel&&voice.note==note)voice.releasing=true;}
    void event(const KernelMidiEvent& event) noexcept {
        const int kind=event.status&0xf0,ch=event.status&15,note=event.a&127,value=event.b&127;
        if(kind==0x90&&value) {
            if(!audio||!audio->valid()||last<=first)return;
            if(down[ch][note]<255)++down[ch][note];
            Voice* chosen=&voices[0];
            for(auto& voice:voices){if(!voice.active){chosen=&voice;break;}if(voice.age<chosen->age)chosen=&voice;}
            *chosen={};chosen->active=true;chosen->note=note;chosen->channel=ch;chosen->age=++serial;
            chosen->position=reverse?double(last-1):double(first);
            chosen->step=std::exp2((note-root)/12.0)*audio->rate/rate;
            chosen->velocity=(1-velocitySensitivity)+velocitySensitivity*float(value)/127;
            chosen->envelope=attack>0?0:1;
        } else if(kind==0x80||kind==0x90) {
            if(down[ch][note])--down[ch][note];if(!down[ch][note]&&!pedal[ch])releaseNote(ch,note);
        } else if(kind==0xe0)bend[ch]=std::exp2(((value*128+note)-8192)*(2.0/(8192*12)));
        else if(kind==0xb0) {
            if(note==64||note==121) {
                pedal[ch]=note==64&&value>=64;if(note==121)bend[ch]=1;
                if(!pedal[ch])for(int n=0;n<128;++n)if(!down[ch][n])releaseNote(ch,n);
            } else if(note==120||note==123) {
                pedal[ch]=false;down[ch]={};
                for(auto& voice:voices)if(voice.channel==ch){if(note==120)voice={};else voice.releasing=true;}
            }
        }
    }
    void render(float& left,float& right) noexcept {
        if(!audio||!audio->valid()||last<=first)return;
        const bool looping=loop&&last>first+1;const double length=double(last-first);
        const double crossfade=looping?std::min(length*.5,xfade*rate):0;
        for(auto& voice:voices) {
            if(!voice.active)continue;
            if(voice.position<double(first)||voice.position>=double(last)){voice.active=false;continue;}
            const size_t i0=size_t(voice.position),i1=i0+1<last?i0+1:(looping?first:last-1);
            const float fraction=float(voice.position-double(i0));
            auto sample=[&](size_t channel){const float a=audio->samples[i0*size_t(audio->channels)+channel];return a+(audio->samples[i1*size_t(audio->channels)+channel]-a)*fraction;};
            float fade=1;
            if(crossfade>0) {
                const double since=reverse?double(last)-voice.position:voice.position-double(first);
                const double remaining=reverse?voice.position-double(first):double(last)-voice.position;
                fade=float(std::min(1.0,std::max(0.0,since)/crossfade)*std::min(1.0,std::max(0.0,remaining)/crossfade));
            }
            const float amplitude=voice.envelope*voice.velocity*gain*fade;
            const float l=sample(0);left+=l*amplitude;right+=(audio->channels>1?sample(1):l)*amplitude;
            if(voice.releasing) {voice.envelope-=float(1/std::max(1.0,release*rate));if(voice.envelope<=0){voice={};continue;}}
            else voice.envelope=std::min(1.f,voice.envelope+float(1/std::max(1.0,attack*rate)));
            voice.position+=voice.step*bend[voice.channel]*(reverse?-1:1);
            if(voice.position>=double(last)||voice.position<double(first)) {
                if(looping) {double position=std::fmod(voice.position-double(first),length);if(position<0)position+=length;voice.position=double(first)+position;}
                else voice.active=false;
            }
        }
    }
public:
    void prepare(double sampleRate,int) override {rate=sampleRate>0?sampleRate:48000;reset(KernelResetReason::Reconfigure);}
    void configure(const KernelConfig& config) override {
        audio=config.audio;root=config.value(SamplerRoot,60);gain=config.value(SamplerGain,.8f);
        loop=config.value(SamplerLoop,0)>=.5f;reverse=config.value(SamplerReverse,0)>=.5f;
        start=std::clamp(double(config.value(SamplerStart,0)),0.0,1.0);end=std::clamp(double(config.value(SamplerEnd,1)),0.0,1.0);
        attack=config.value(SamplerAttack,.005f);release=config.value(SamplerRelease,.12f);xfade=config.value(SamplerXfade,5)*.001;
        velocitySensitivity=std::clamp(config.value(SamplerVelocity,1),0.f,1.f);
        const size_t frames=audio&&audio->valid()?audio->frames():0;
        first=frames?size_t(start*double(frames-1)):0;last=frames?std::max(first+1,size_t(end*double(frames))):0;
        reset(KernelResetReason::Reconfigure);
    }
    void reset(KernelResetReason) noexcept override {voices={};down={};pedal={};bend.fill(1);serial=0;}
    void process(KernelContext& context) noexcept override {
        auto* output=context.output("audio");const auto* input=context.input("notes");size_t cursor=0;
        if(context.bypassed||!input)reset(KernelResetReason::Stop);
        for(int frame=0;frame<context.frames;++frame) {
            if(!context.bypassed)while(input&&input->midi&&cursor<input->midiCount&&input->midi[cursor].frame<=frame)event(input->midi[cursor++]);
            float left=0,right=0;if(!context.bypassed)render(left,right);
            if(output){if(output->left)output->left[frame]=left;if(output->right)output->right[frame]=right;}
        }
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
