#pragma once
#include "../NodeKernel.hpp"
#include "MidiControllerState.hpp"
#include <algorithm>
#include <array>
namespace tri {
// Bounded note state shared by MIDI kernels. No graph, node, or UI dependency.
struct MidiHeldNote {uint8_t velocity=0;uint64_t token=0;};
class MidiNoteState {
public:
    std::array<MidiHeldNote,2048> notes{};
    uint64_t serial=0;
    MidiControllerState controllers;
    void clear() noexcept {notes.fill({});controllers=MidiControllerState{};serial=0;}
    void observe(KernelMidiEvent e) noexcept {
        if(e.a>127||e.b>127)return;
        const int kind=e.status&0xf0,key=int(e.a)*16+(e.status&15);
        if(kind==0x90&&e.b)notes[key]={e.b,++serial};
        else if(kind==0x80||kind==0x90)notes[key]={};
        else if(kind==0xb0&&(e.a==120||e.a>=123))for(int k=e.status&15;k<2048;k+=16)notes[k]={};
        controllers.observe(e.status,e.a,e.b);
    }
    bool emit(KernelOutput* out,KernelMidiEvent e) noexcept {
        if(!out||!out->emitMidi(e))return false;observe(e);return true;
    }
    void release(KernelOutput* out,int frame=0) noexcept {
        if(!out)return;
        for(int k=0;k<2048;++k)if(notes[k].velocity)
            emit(out,{frame,uint8_t(0x80|(k%16)),uint8_t(k/16),0});
        const auto previous=controllers;bool controlsSent=true;
        controllers.release([&](uint8_t s,uint8_t a,uint8_t b){if(!out->emitMidi({frame,s,a,b}))controlsSent=false;});
        if(!controlsSent)controllers=previous;
    }
    void synchronize(KernelOutput* out,const std::array<MidiHeldNote,2048>& desired,int frame=0) noexcept {
        if(!out)return;
        for(int k=0;k<2048;++k)if(notes[k].velocity&&(!desired[k].velocity||notes[k].token!=desired[k].token)) {
            if(out->emitMidi({frame,uint8_t(0x80|(k%16)),uint8_t(k/16),0}))notes[k]={};
        }
        for(int k=0;k<2048;++k)if(desired[k].velocity&&!notes[k].velocity) {
            if(out->emitMidi({frame,uint8_t(0x90|(k%16)),uint8_t(k/16),desired[k].velocity}))notes[k]=desired[k];
        }
    }
};
// A one-pitch-per-input transform keeps the original Note Off destination and
// reference counts collapsed pitches, including clamped range endpoints.
class MidiPitchMapper {
    std::array<int16_t,2048> mapping{};
    std::array<uint16_t,2048> counts{};
    MidiNoteState input,output;
    bool resetPending=false;
    void releaseKey(KernelOutput* out,int key,int frame,uint8_t velocity=0) noexcept {
        const int mapped=mapping[key];if(mapped<0)return;
        mapping[key]=-1;
        if(counts[mapped]&&!--counts[mapped])output.emit(out,{frame,uint8_t(0x80|(mapped%16)),uint8_t(mapped/16),velocity});
    }
    template<class Map> void startKey(KernelOutput* out,int key,uint8_t velocity,int frame,Map map) noexcept {
        const int pitch=map(key/16,key%16);if(pitch<0||pitch>127)return;
        const int target=pitch*16+key%16;
        if(!counts[target]&&!output.emit(out,{frame,uint8_t(0x90|(key%16)),uint8_t(pitch),velocity}))return;
        mapping[key]=int16_t(target);++counts[target];
    }
public:
    MidiPitchMapper(){mapping.fill(-1);}
    void reset() noexcept {input.clear();output.clear();mapping.fill(-1);counts.fill(0);resetPending=false;}
    template<class Map> void process(KernelContext& c,Map map,bool revoice=false) noexcept {
        auto* out=c.output("notes");const auto* in=c.input("notes");
        if(resetPending||revoice||!in) {
            output.release(out);mapping.fill(-1);counts.fill(0);
            if(!in)input.clear();
            if(!resetPending&&in)for(int k=0;k<2048;++k)if(input.notes[k].velocity)startKey(out,k,input.notes[k].velocity,0,map);
            resetPending=false;
        }
        if(!in)return;
        for(size_t i=0;i<in->midiCount;++i) {
            const auto e=in->midi[i];if(e.a>127||e.b>127)continue;
            const int kind=e.status&0xf0,key=int(e.a)*16+(e.status&15);
            if(kind==0x90&&e.b){releaseKey(out,key,e.frame);input.observe(e);startKey(out,key,e.b,e.frame,map);}
            else if(kind==0x80||kind==0x90){input.observe(e);releaseKey(out,key,e.frame,e.b);}
            else {
                input.observe(e);output.emit(out,e);
                if(kind==0xb0&&(e.a==120||e.a>=123))for(int k=e.status&15;k<2048;k+=16){mapping[k]=-1;counts[k]=0;}
            }
        }
        if(out&&out->overflow)resetPending=true;
    }
};
}
