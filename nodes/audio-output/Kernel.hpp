#pragma once
#include "Parameters.hpp"
namespace tri::nodes::audio_output {
class Kernel final:public NodeKernel {
    float gain=1,width=1;
public:
    void prepare(double,int) override{}
    void configure(const KernelConfig& c) override{gain=c.value(Master,.74f);width=c.value(OutputWidth,1);}
    void reset(KernelResetReason) noexcept override{}
    void process(KernelContext& c) noexcept override{
        const auto* in=c.input("audio");auto* out=c.output("audio");if(!out)return;
        for(int i=0;i<c.frames;++i){float l=!c.bypassed&&in&&in->left?in->left[i]:0,r=!c.bypassed&&in&&in->right?in->right[i]:0;
            if(!c.bypassed){const float mid=(l+r)*.5f,side=(l-r)*.5f*width;l=(mid+side)*gain;r=(mid-side)*gain;}
            if(out->left)out->left[i]=l;if(out->right)out->right[i]=r;}
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
