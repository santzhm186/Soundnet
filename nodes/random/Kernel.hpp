#pragma once
#include "Parameters.hpp"
#include "../../platform/NodeKernel.hpp"
#include <algorithm>
namespace tri::nodes::random {
class Kernel final : public NodeKernel {
    uint64_t state=0x9e3779b97f4a7c15ull;float chance=.5f;bool seeded=false;
    float next() noexcept {state^=state>>12;state^=state<<25;state^=state>>27;return float((state*2685821657736338717ull)>>40)*(1.f/16777216.f);}
public:
    void prepare(double,int) override {seeded=false;}
    void configure(const KernelConfig& c) override {chance=std::clamp(c.value(RandomChance,.5f),0.f,1.f);}
    void reset(KernelResetReason) noexcept override {}
    void process(KernelContext& c) noexcept override {
        if(!seeded){state^=c.instanceID*0xbf58476d1ce4e5b9ull;if(!state)state=1;seeded=true;}
        const auto* in=c.input("trigger");auto* out=c.output("trigger");if(!in||!out)return;
        for(size_t i=0;i<in->triggerCount;++i)if(next()<chance)out->emitTrigger(in->triggers[i]);
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
