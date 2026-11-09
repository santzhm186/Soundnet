#pragma once
#include "../../platform/NodeKernel.hpp"
namespace tri::nodes::gain {
inline constexpr int amountID=parameterID("soundnet.gain","gain");
class Kernel final : public NodeKernel {
    float amount=1;
public:
    void prepare(double,int) override {}
    void configure(const KernelConfig& config) override {amount=config.value(amountID,1);}
    void reset(KernelResetReason) noexcept override {}
    void process(KernelContext& context) noexcept override {
        const auto* input=context.input("audio");auto* output=context.output("audio");
        if(!output)return;
        const float level=context.bypassed?1:amount;
        for(int i=0;i<context.frames;++i) {
            if(output->left)output->left[i]=input&&input->left?input->left[i]*level:0;
            if(output->right)output->right[i]=input&&input->right?input->right[i]*level:0;
        }
    }
};
inline std::unique_ptr<NodeKernel> create() {return std::make_unique<Kernel>();}
}
