#pragma once
#include "Parameters.hpp"
#include "../../platform/NodeKernel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace tri::nodes::wait {
class Kernel final : public NodeKernel {
    struct Pending {uint64_t due=0;bool start=false;};
    std::array<Pending,4096> queue{};size_t size=0;
    double rate=48000,seconds=1;uint64_t clock=0;
public:
    void prepare(double r,int) override {rate=r>0?r:48000;size=0;clock=0;}
    void configure(const KernelConfig& c) override {seconds=std::clamp(double(c.value(WaitSeconds,1)),0.0,3600.0);}
    void reset(KernelResetReason) noexcept override {size=0;clock=0;}
    void process(KernelContext& c) noexcept override {
        auto* out=c.output("trigger");
        if(const auto* in=c.input("trigger"))for(size_t i=0;i<in->triggerCount;++i) {
            if(size==queue.size()){if(out)out->overflow=true;continue;}
            Pending value{clock+uint64_t(std::max(0,in->triggers[i].frame))+uint64_t(std::llround(seconds*rate)),in->triggers[i].start};
            size_t p=size++;while(p&&queue[p-1].due>value.due){queue[p]=queue[p-1];--p;}queue[p]=value;
        }
        size_t used=0;const uint64_t end=clock+uint64_t(std::max(0,c.frames));
        while(used<size&&queue[used].due<end) {
            if(out)out->emitTrigger({queue[used].due>clock?int(queue[used].due-clock):0,queue[used].start});++used;
        }
        for(size_t i=used;i<size;++i)queue[i-used]=queue[i];size-=used;clock=end;
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
