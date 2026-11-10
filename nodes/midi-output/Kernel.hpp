#pragma once
#include "Parameters.hpp"
namespace tri::nodes::midi_output {
class Kernel final:public NodeKernel {
    int channel=0;
public:
    void prepare(double,int) override{}
    void configure(const KernelConfig& c) override{channel=int(c.value(MidiOutChannel,0));}
    void reset(KernelResetReason) noexcept override{}
    void process(KernelContext& c) noexcept override{
        const auto* in=c.input("notes");auto* out=c.output("notes");if(!in)return;
        for(size_t i=0;i<in->midiCount;++i){auto e=in->midi[i];
            if(e.status<0xf0&&channel>0)e.status=uint8_t((e.status&0xf0)|(channel-1));
            if(out)out->emitMidi(e);if(c.hostMidi)c.hostMidi->emitMidi(e);}
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
