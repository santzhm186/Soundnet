#pragma once
#include "../../platform/runtime/ProcessorKernel.hpp"
#include "Processor.hpp"
namespace tri::nodes::tremolo {
class Kernel final : public ProcessorKernel<TremoloProcessor> {
public:
    void configure(const KernelConfig& config) override {
        ProcessorKernel<TremoloProcessor>::configure(config);
        if(config.value(TremoloSync,0)>=.5f) {
            constexpr double divisions[]={1,2,4,1.5};
            const double hz=std::clamp(double(config.value(ClockBPM,120))/60.0*divisions[std::clamp(int(config.value(TremoloDivision,0)),0,3)],.1,20.0);
            processor.set(TremoloRate,float(hz));
        }
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
