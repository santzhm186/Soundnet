#pragma once
#include "Parameters.hpp"
namespace tri::nodes::audio_input {
class Kernel final:public NodeKernel {
    float gain=1;
public:
    void prepare(double,int) override {}
    void configure(const KernelConfig& c) override{gain=c.value(InputGain,1);}
    void reset(KernelResetReason) noexcept override{}
    void process(KernelContext& c) noexcept override{
        auto* out=c.output("audio");if(!out)return;
        const float* right=c.hostAudioRight?c.hostAudioRight:c.hostAudioLeft;
        for(int i=0;i<c.frames;++i){if(out->left)out->left[i]=c.hostAudioLeft?c.hostAudioLeft[i]*gain:0;if(out->right)out->right[i]=right?right[i]*gain:0;}
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
