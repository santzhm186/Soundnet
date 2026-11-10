#pragma once
#include "Parameters.hpp"
#include "../../platform/runtime/MidiNoteState.hpp"
#include <algorithm>
#include <cmath>
namespace tri::nodes::midi_clip {
class Kernel final : public NodeKernel {
    std::shared_ptr<const MidiClip> clip;MidiNoteState output;std::array<uint16_t,2048> down{};
    double rate=48000,bpm=112,position=0;size_t cursor=0;bool running=false,configuredRun=false,loop=true,clearPending=true,seekPending=false,paused=false;
    void clear(KernelOutput* out,int frame) noexcept {output.release(out,frame);down.fill(0);}
    void chase(KernelOutput* out) noexcept {
        if(!clip)return;
        std::array<uint8_t,2048> velocities{};
        struct Control {uint8_t status=0,a=0,b=0;};
        std::array<Control,16*259> controls{};
        down.fill(0);
        while(cursor<clip->events.size()&&clip->events[cursor].beat<position-1e-9) {
            const auto e=clip->events[cursor++];const int kind=e.status&0xf0,ch=e.status&15,k=int(e.data1)*16+ch;
            if(e.data1>127||e.data2>127)continue;
            if(kind==0x90&&e.data2){if(down[k]<65535)++down[k];velocities[k]=e.data2;}
            else if(kind==0x80||kind==0x90){if(down[k])--down[k];if(!down[k])velocities[k]=0;}
            else {
                int slot=kind==0xb0?e.data1:kind==0xa0?128+e.data1:kind==0xc0?256:kind==0xd0?257:kind==0xe0?258:-1;
                if(slot>=0)controls[ch*259+slot]={e.status,e.data1,e.data2};
                if(kind==0xb0&&(e.data1==120||e.data1>=123))for(int n=ch;n<2048;n+=16){down[n]=0;velocities[n]=0;}
            }
        }
        for(const auto e:controls)if(e.status)output.emit(out,{0,e.status,e.a,e.b});
        for(int k=0;k<2048;++k)if(velocities[k])output.emit(out,{0,uint8_t(0x90|(k%16)),uint8_t(k/16),velocities[k]});
    }
    void event(KernelOutput* out,const ClipEvent& e,int frame) noexcept {
        const int kind=e.status&0xf0,k=int(e.data1)*16+(e.status&15);
        if(e.data1>127||e.data2>127)return;
        if(kind==0x90&&e.data2) {
            if(down[k]&&output.notes[k].velocity)output.emit(out,{frame,uint8_t(0x80|(e.status&15)),e.data1,0});
            if(output.emit(out,{frame,e.status,e.data1,e.data2})&&down[k]<65535)++down[k];
        }else if(kind==0x80||kind==0x90) {
            if(down[k])--down[k];
            if(!down[k])output.emit(out,{frame,uint8_t(0x80|(e.status&15)),e.data1,e.data2});
        }else {
            output.emit(out,{frame,e.status,e.data1,e.data2});
            if(kind==0xb0&&(e.data1==120||e.data1>=123))for(int n=e.status&15;n<2048;n+=16)down[n]=0;
        }
    }
public:
    void prepare(double r,int) override {rate=r>0?r:48000;output.clear();down.fill(0);position=0;cursor=0;clearPending=true;paused=false;}
    void configure(const KernelConfig& c) override {
        clip=c.clip;bpm=std::clamp(double(c.value(ClockBPM,112)),40.0,240.0);
        loop=c.value(ClipLoop,1)>.5f;running=configuredRun=c.value(ClipRun,0)>.5f;position=0;cursor=0;clearPending=true;seekPending=running;paused=false;
    }
    void reset(KernelResetReason reason) noexcept override {output.clear();down.fill(0);running=reason==KernelResetReason::Seek&&(running||configuredRun);position=0;cursor=0;clearPending=false;seekPending=reason==KernelResetReason::Seek;paused=false;}
    void process(KernelContext& c) noexcept override {
        auto* out=c.output("notes");auto* cycle=c.output("cycle");const auto* trigger=c.input("trigger");size_t ti=0;
        if(clearPending){clear(out,0);clearPending=false;}
        bool chaseNeeded=false;
        if(seekPending) {
            seekPending=false;position=double(c.framePosition)*bpm/(60*rate);cursor=0;
            if(clip&&clip->lengthBeats>0&&loop)position=std::fmod(position,clip->lengthBeats);
            if(clip&&!loop&&position>=clip->lengthBeats)running=false;
            chaseNeeded=running;
        }
        // Host pause is distinct from a trigger Stop: retain the run state
        // and musical cursor, but release every note/controller already sent.
        // A paused seek updates the cursor now and defers its chase to resume.
        if(!c.playing) {
            if(!paused)clear(out,0);
            paused=true;
        }else {
            if(paused){paused=false;cursor=0;chaseNeeded=running;}
            if(chaseNeeded)chase(out);
        }
        const double increment=bpm/(60*rate);
        for(int f=0;f<c.frames;++f) {
            while(trigger&&ti<trigger->triggerCount&&trigger->triggers[ti].frame<=f) {
                clear(out,f);running=trigger->triggers[ti++].start;position=0;cursor=0;
            }
            if(!c.playing||!running||!clip||clip->lengthBeats<=0||clip->events.empty())continue;
            if(position+1e-9>=clip->lengthBeats) {
                while(cursor<clip->events.size()&&clip->events[cursor].beat<=clip->lengthBeats+1e-9)event(out,clip->events[cursor++],f);
                clear(out,f);if(cycle)cycle->emitTrigger({f,true});
                if(!loop){running=false;continue;}
                position=std::fmod(std::max(0.0,position-clip->lengthBeats),clip->lengthBeats);cursor=0;
            }
            while(cursor<clip->events.size()&&clip->events[cursor].beat<=position+1e-9)event(out,clip->events[cursor++],f);
            position+=increment;
        }
        if(out&&out->overflow){running=false;clearPending=true;}
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
