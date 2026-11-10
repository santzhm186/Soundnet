#pragma once
#include "Parameters.hpp"
#include "../../platform/NodeKernel.hpp"
#include <algorithm>
namespace tri::nodes::count {
class Kernel final : public NodeKernel {
    int count=0,threshold=4;
public:
    void prepare(double,int) override {count=0;}
    void configure(const KernelConfig& c) override {threshold=std::clamp(int(c.value(TriggerCount,4)),1,1000000);count=0;}
    void reset(KernelResetReason) noexcept override {count=0;}
    void process(KernelContext& c) noexcept override {
        auto* out=c.output("trigger");const auto* in=c.input("trigger");if(!in||!out)return;
        for(size_t i=0;i<in->triggerCount;++i)if(++count>=threshold){count=0;out->emitTrigger(in->triggers[i]);}
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
