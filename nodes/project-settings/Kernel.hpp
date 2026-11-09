#pragma once
#include "../../platform/NodeKernel.hpp"
namespace tri::nodes::project_settings {
// The host applies settings before playback. This environment node declares no
// stream output and never changes devices or allocates from process().
class Kernel final : public NodeKernel {
public:
    void prepare(double,int) override {}
    void configure(const KernelConfig&) override {}
    void reset(KernelResetReason) noexcept override {}
    void process(KernelContext&) noexcept override {}
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
