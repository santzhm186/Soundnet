#pragma once
#include "Parameters.hpp"
#include "../../platform/runtime/MidiNoteState.hpp"
#include <algorithm>
namespace tri::nodes::channel_filter {
class Kernel final : public NodeKernel {
    MidiNoteState input,output;int channel=0;bool dirty=true,lastBypass=false;
public:
    void prepare(double,int) override {input.clear();output.clear();dirty=true;}
    void configure(const KernelConfig& c) override {channel=std::clamp(int(c.value(ChannelFilter,0)),0,16);dirty=true;}
    void reset(KernelResetReason) noexcept override {input.clear();output.clear();dirty=true;}
    void process(KernelContext& c) noexcept override {
        const auto* in=c.input("notes");auto* out=c.output("notes");
        auto allowed=[&](int ch){return c.bypassed||channel==0||ch==channel-1;};
        if(dirty||c.bypassed!=lastBypass||!in) {
            output.release(out);if(!in)input.clear();
            for(int k=0;k<2048;++k)if(input.notes[k].velocity&&allowed(k%16))output.emit(out,{0,uint8_t(0x90|(k%16)),uint8_t(k/16),input.notes[k].velocity});
            dirty=false;lastBypass=c.bypassed;
        }
        if(in)for(size_t i=0;i<in->midiCount;++i){const auto e=in->midi[i];input.observe(e);if(allowed(e.status&15))output.emit(out,e);}
        if(out&&out->overflow)dirty=true;
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
