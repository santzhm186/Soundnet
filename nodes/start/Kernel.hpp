#pragma once
#include "Parameters.hpp"
#include "../../platform/NodeKernel.hpp"
namespace tri::nodes::start {
class Kernel final : public NodeKernel {
    int pulse=0;bool configured=false,pending=false;
public:
    void prepare(double,int) override {}
    void configure(const KernelConfig& c) override {
        const int next=int(c.value(TriggerPulse,0));
        auto previous=c.previousParameters.find(TriggerPulse);
        if((configured&&next!=pulse)||(c.hasPreviousConfiguration&&previous!=c.previousParameters.end()&&next!=int(previous->second)))pending=true;
        pulse=next;configured=true;
    }
    void reset(KernelResetReason) noexcept override {pending=false;}
    void process(KernelContext& c) noexcept override {
        auto* out=c.output("trigger");if(!out)return;
        if(pending&&out->emitTrigger({0,true}))pending=false;
        if(const auto* in=c.input("trigger"))for(size_t i=0;i<in->triggerCount;++i)
            out->emitTrigger({in->triggers[i].frame,true});
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
