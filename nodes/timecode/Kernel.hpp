#pragma once
#include "../../platform/NodeKernel.hpp"
namespace tri::nodes::timecode {
class Kernel final : public NodeKernel {
public:
    void prepare(double,int) override {}
    void configure(const KernelConfig&) override {}
    void reset(KernelResetReason) noexcept override {}
    void process(KernelContext& context) noexcept override {
        auto* output=context.output("time");
        if(output&&output->timecode)*output->timecode={context.framePosition,context.timeGeneration,context.sampleRate,context.playing};
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
