#pragma once
#include "../../platform/NodeKernel.hpp"
namespace tri::nodes::midi_input {
class Kernel final:public NodeKernel {
public:
    void prepare(double,int) override{}
    void configure(const KernelConfig&) override{}
    void reset(KernelResetReason) noexcept override{}
    void process(KernelContext& c) noexcept override{
        auto* out=c.output("notes");if(!out)return;
        for(size_t i=0;i<c.localMidiCount;++i)out->emitMidi(c.localMidi[i]);
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
