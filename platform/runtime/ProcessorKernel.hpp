#pragma once
#include "../AudioProcessor.hpp"
#include <algorithm>
#include <vector>
namespace tri {
// Adapts a node-owned DSP implementation to the public block lifecycle.
template<class Processor> class ProcessorKernel : public NodeKernel {
protected:
    Processor processor;
    std::vector<float> silence;
public:
    void prepare(double sampleRate,int maxFrames) override {
        silence.assign(size_t(std::max(1,maxFrames)),0.f);
        processor.prepare(sampleRate);
    }
    void configure(const KernelConfig& config) override {
        for(const auto& parameter:config.parameters)processor.set(parameter.first,parameter.second);
    }
    void reset(KernelResetReason) noexcept override {processor.reset();}
    void process(KernelContext& context) noexcept override {
        auto* output=context.output("audio");
        if(!output||!output->left||!output->right||context.frames<=0)return;
        const auto* input=context.input("audio");
        const auto* detector=context.input("sidechain");
        for(int offset=0;offset<context.frames;) {
            const int count=std::min(context.frames-offset,int(silence.size()));
            if(count<=0)return;
            const float* left=input&&input->left?input->left+offset:silence.data();
            const float* right=input&&input->right?input->right+offset:silence.data();
            if(context.bypassed) {
                std::copy_n(left,count,output->left+offset);
                std::copy_n(right,count,output->right+offset);
            } else {
                processor.setSidechain(detector&&detector->left?detector->left+offset:nullptr,
                    detector&&detector->right?detector->right+offset:nullptr,count);
                processor.render(left,right,output->left+offset,output->right+offset,count);
                processor.setSidechain(nullptr,nullptr,0);
            }
            offset+=count;
        }
    }
};
}
