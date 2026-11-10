#pragma once
#include "../../platform/NodeKernel.hpp"
#include "Parameters.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace tri::nodes::oscillator {
class Kernel final : public NodeKernel {
    enum Stage { Idle,AttackStage,DecayStage,SustainStage,ReleaseStage };
    struct Voice {Stage stage=Idle;int channel=0,note=0;uint64_t age=0;double phase=0,progress=0;float velocity=0,envelope=0,releaseFrom=0;};
    std::array<Voice,24> voices{};
    std::array<std::array<unsigned,128>,16> down{};
    std::array<bool,16> sustain{};
    std::array<double,16> bend{};
    double rate=48000,tune=1,attack=.012,decay=.22,release=.24;
    float level=.66f,sustainLevel=.7f;int shape=2;uint64_t serial=0;
    static float blep(double phase,double increment) noexcept {
        if(phase<increment){const double t=phase/increment;return float(2*t-t*t-1);}
        if(phase>1-increment){const double t=(phase-1)/increment;return float(t*t+2*t+1);}
        return 0;
    }
    static float wave(int shape,double phase,double increment) noexcept {
        if(shape==0)return float(std::sin(6.28318530717958647692*phase));
        if(shape==1)return (phase<.5?1.f:-1.f)+blep(phase,increment)-blep(phase<.5?phase+.5:phase-.5,increment);
        if(shape==2)return float(2*phase-1)-blep(phase,increment);
        return float(1-4*std::abs(phase-.5));
    }
    static void releaseVoice(Voice& voice) noexcept {
        if(voice.stage==Idle||voice.stage==ReleaseStage)return;
        voice.releaseFrom=voice.envelope;voice.progress=0;voice.stage=ReleaseStage;
    }
    void releaseNote(int channel,int note) noexcept {for(auto& voice:voices)if(voice.channel==channel&&voice.note==note)releaseVoice(voice);}
    void event(const KernelMidiEvent& event) noexcept {
        const int kind=event.status&0xf0,ch=event.status&15,note=event.a&127,value=event.b&127;
        if(kind==0x90&&value) {
            if(down[ch][note]<255)++down[ch][note];releaseNote(ch,note);
            Voice* chosen=&voices[0];
            for(auto& voice:voices) {
                if(voice.stage==Idle){chosen=&voice;break;}
                if((voice.stage==ReleaseStage&&chosen->stage!=ReleaseStage)||
                    (voice.stage==ReleaseStage&&chosen->stage==ReleaseStage&&voice.envelope<chosen->envelope)||
                    (voice.stage!=ReleaseStage&&chosen->stage!=ReleaseStage&&voice.age<chosen->age))chosen=&voice;
            }
            *chosen={};chosen->stage=AttackStage;chosen->channel=ch;chosen->note=note;chosen->age=++serial;chosen->velocity=float(value)/127;
        } else if(kind==0x80||kind==0x90) {
            if(down[ch][note])--down[ch][note];
            if(!down[ch][note]&&!sustain[ch])releaseNote(ch,note);
        } else if(kind==0xe0)bend[ch]=std::exp2(((value*128+note)-8192)*(2.0/(8192*12)));
        else if(kind==0xb0) {
            if(note==64||note==121) {
                sustain[ch]=note==64&&value>=64;if(note==121)bend[ch]=1;
                if(!sustain[ch])for(int n=0;n<128;++n)if(!down[ch][n])releaseNote(ch,n);
            } else if(note==120||note==123) {
                sustain[ch]=false;down[ch]={};
                for(auto& voice:voices)if(voice.channel==ch){if(note==120)voice={};else releaseVoice(voice);}
            }
        }
    }
    float sample() noexcept {
        float result=0;
        for(auto& voice:voices) {
            switch(voice.stage) {
                case Idle:continue;
                case AttackStage:voice.progress+=1/std::max(1.0,rate*attack);voice.envelope=float(std::min(1.0,voice.progress));if(voice.progress>=1){voice.stage=DecayStage;voice.progress=0;}break;
                case DecayStage:voice.progress+=1/std::max(1.0,rate*decay);voice.envelope=1+float(std::min(1.0,voice.progress))*(sustainLevel-1);if(voice.progress>=1)voice.stage=SustainStage;break;
                case SustainStage:voice.envelope=sustainLevel;break;
                case ReleaseStage:voice.progress+=1/std::max(1.0,rate*release);voice.envelope=voice.releaseFrom*float(std::max(0.0,1-voice.progress));if(voice.progress>=1)voice.stage=Idle;break;
            }
            const double increment=440*std::exp2((voice.note-69)/12.0)*tune*bend[voice.channel]/rate;
            if(increment<=0||increment>=.49)continue;
            result+=voice.velocity*.25f*voice.envelope*level*wave(shape,voice.phase,increment);
            voice.phase+=increment;voice.phase-=std::floor(voice.phase);
        }
        return result;
    }
public:
    void prepare(double sampleRate,int) override {rate=sampleRate>0?sampleRate:48000;reset(KernelResetReason::Reconfigure);}
    void configure(const KernelConfig& config) override {
        shape=std::clamp(int(config.value(Wave1,2)),0,3);level=config.value(Level1,.66f);tune=std::exp2(config.value(Tune1,0)/12.0);
        attack=config.value(Attack,.012f);decay=config.value(Decay,.22f);sustainLevel=config.value(Sustain,.7f);release=config.value(Release,.24f);
    }
    void reset(KernelResetReason) noexcept override {voices={};down={};sustain={};bend.fill(1);serial=0;}
    void process(KernelContext& context) noexcept override {
        auto* output=context.output("audio");const auto* input=context.input("notes");size_t cursor=0;
        if(!input)reset(KernelResetReason::Removed);
        for(int frame=0;frame<context.frames;++frame) {
            while(input&&input->midi&&cursor<input->midiCount&&input->midi[cursor].frame<=frame)event(input->midi[cursor++]);
            const float value=sample();if(output){if(output->left)output->left[frame]=value;if(output->right)output->right[frame]=value;}
        }
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
