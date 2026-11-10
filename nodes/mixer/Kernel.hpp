#pragma once
#include "../../platform/NodeKernel.hpp"
namespace tri::nodes::mixer {
class Kernel final : public NodeKernel {
public:
    void prepare(double,int) override {}
    void configure(const KernelConfig&) override {}
    void reset(KernelResetReason) noexcept override {}
    void process(KernelContext& context) noexcept override {
        auto* output=context.output("audio");if(!output)return;
        for(int frame=0;frame<context.frames;++frame) {
            float left=0,right=0;
            for(const char* port:{"in1","in2","in3","in4"}) {
                const auto* input=context.input(port);
                if(input) {left+=input->left?input->left[frame]:0;right+=input->right?input->right[frame]:0;}
            }
            if(output->left)output->left[frame]=left;
            if(output->right)output->right[frame]=right;
        }
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
