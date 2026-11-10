#pragma once
#include "Parameters.hpp"
#include "../../platform/runtime/MidiNoteState.hpp"
#include <algorithm>
namespace tri::nodes::voice_arranger {
class Kernel final : public NodeKernel {
    MidiNoteState input;std::array<MidiNoteState,3> output;
    std::array<int,3> minimum{72,55,36},maximum{95,71,54},texture{0,1,2};
    std::array<MidiHeldNote,2048> desired{};bool dirty=true;uint64_t previousToken=0;
    int fold(int pitch,int v) const noexcept {
        if(pitch<minimum[v])pitch+=12*((minimum[v]-pitch+11)/12);
        else if(pitch>maximum[v])pitch-=12*((pitch-maximum[v]+11)/12);
        return pitch>=minimum[v]&&pitch<=maximum[v]?pitch:-1;
    }
    void revoice(KernelContext& c,int frame) noexcept {
        const char* ports[]={"high","middle","low"};
        for(int v=0;v<3;++v) {
            desired.fill({});std::array<bool,2048> continuing{};std::array<int,2048> playable{};int size=0;
            auto add=[&](int k){const int pitch=fold(k/16,v);if(pitch<0)return;const int target=pitch*16+k%16;
                desired[target].velocity=std::max(desired[target].velocity,input.notes[k].velocity);
                desired[target].token=std::max(desired[target].token,input.notes[k].token);continuing[target]|=input.notes[k].token<=previousToken;};
            for(int k=0;k<2048;++k)if(input.notes[k].velocity&&fold(k/16,v)>=0) {
                if(texture[v]==3)add(k);else playable[size++]=k;
            }
            if(size)add(playable[texture[v]==0?size-1:texture[v]==1?(size-1)/2:0]);
            for(int k=0;k<2048;++k)if(continuing[k]&&output[v].notes[k].velocity)desired[k].token=output[v].notes[k].token;
            output[v].synchronize(c.output(ports[v]),desired,frame);
        }
        previousToken=input.serial;
    }
public:
    void prepare(double,int) override {input.clear();for(auto& out:output)out.clear();dirty=true;}
    void configure(const KernelConfig& c) override {
        const int lo[]={ArrHighMin,ArrMidMin,ArrLowMin},hi[]={ArrHighMax,ArrMidMax,ArrLowMax},tx[]={ArrHighTexture,ArrMidTexture,ArrLowTexture};
        const int ld[]={72,55,36},hd[]={95,71,54};
        for(int v=0;v<3;++v){minimum[v]=std::clamp(int(c.value(lo[v],ld[v])),0,127);maximum[v]=std::clamp(int(c.value(hi[v],hd[v])),minimum[v],127);texture[v]=std::clamp(int(c.value(tx[v],v)),0,3);}dirty=true;
    }
    void reset(KernelResetReason) noexcept override {input.clear();for(auto& out:output)out.clear();previousToken=0;dirty=true;}
    void process(KernelContext& c) noexcept override {
        const auto* in=c.input("notes");if(!in){input.clear();previousToken=0;dirty=true;}
        if(dirty){revoice(c,0);dirty=false;}
        size_t cursor=0;
        while(in&&cursor<in->midiCount) {
            const int frame=in->midi[cursor].frame;
            do {const auto e=in->midi[cursor++];input.observe(e);const int kind=e.status&0xf0;
                if(kind!=0x80&&kind!=0x90){const char* ports[]={"high","middle","low"};for(int v=0;v<3;++v)output[v].emit(c.output(ports[v]),e);}
            }while(cursor<in->midiCount&&in->midi[cursor].frame==frame);
            revoice(c,frame);
        }
        for(size_t i=0;i<c.outputCount;++i)if(c.outputs[i].overflow)dirty=true;
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
