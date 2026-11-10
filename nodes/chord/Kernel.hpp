#pragma once
#include "Parameters.hpp"
#include "../../platform/runtime/MidiNoteState.hpp"
#include "../../platform/music/Harmony.hpp"
#include <algorithm>
namespace tri::nodes::chord {
class Kernel final : public NodeKernel {
    MidiNoteState input,output;int root=60,quality=3;bool run=false,configuredRun=false,dirty=true;uint64_t previousToken=0;
    std::array<MidiHeldNote,2048> desired{};
    void revoice(KernelOutput* out,int frame,bool bypass) noexcept {
        desired.fill({});std::array<bool,2048> continuing{};
        auto add=[&](int pitch,int channel,uint8_t velocity,uint64_t token,bool continues) {
            if(pitch<0||pitch>127)return;const int k=pitch*16+channel;
            desired[k].velocity=std::max(desired[k].velocity,velocity);
            desired[k].token=std::max(desired[k].token,token);continuing[k]|=continues;
        };
        for(int k=0;k<2048;++k)if(input.notes[k].velocity) {
            if(bypass)add(k/16,k%16,input.notes[k].velocity,input.notes[k].token,input.notes[k].token<=previousToken);
            else for(int d:chordIntervals[quality])if(d>=0)add(k/16+d,k%16,input.notes[k].velocity,input.notes[k].token,input.notes[k].token<=previousToken);
        }
        if(run&&!bypass)for(int d:chordIntervals[quality])if(d>=0)add(root+d,0,100,1,true);
        for(int k=0;k<2048;++k)if(continuing[k]&&output.notes[k].velocity)desired[k].token=output.notes[k].token;
        output.synchronize(out,desired,frame);previousToken=input.serial;
    }
public:
    void prepare(double,int) override {input.clear();output.clear();dirty=true;}
    void configure(const KernelConfig& c) override {root=std::clamp(int(c.value(ChordRoot,60)),24,96);quality=std::clamp(int(c.value(ChordQuality,3)),0,7);run=configuredRun=c.value(ChordRun,0)>.5f;dirty=true;}
    void reset(KernelResetReason reason) noexcept override {input.clear();output.clear();previousToken=0;run=reason==KernelResetReason::Seek&&configuredRun;dirty=true;}
    void process(KernelContext& c) noexcept override {
        auto* out=c.output("notes");const auto* in=c.input("notes");
        if(!in){input.clear();previousToken=0;dirty=true;}
        if(dirty){revoice(out,0,c.bypassed);dirty=false;}
        size_t cursor=0;
        while(in&&cursor<in->midiCount) {
            const int frame=in->midi[cursor].frame;
            do {auto e=in->midi[cursor++];input.observe(e);const int kind=e.status&0xf0;
                if(kind!=0x80&&kind!=0x90)output.emit(out,e);
            }while(cursor<in->midiCount&&in->midi[cursor].frame==frame);
            revoice(out,frame,c.bypassed);
        }
        if(out&&out->overflow)dirty=true;
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
