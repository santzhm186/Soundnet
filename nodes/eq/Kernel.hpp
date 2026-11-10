#pragma once
#include "../../platform/NodeKernel.hpp"
#include "Parameters.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace tri::nodes::eq {
class Kernel final : public NodeKernel {
    struct Biquad {
        double b0=1,b1=0,b2=0,a1=0,a2=0;
        std::array<double,2> z1{},z2{};
        float tick(float input,int channel) noexcept {
            const double output=b0*input+z1[channel];
            z1[channel]=b1*input-a1*output+z2[channel];z2[channel]=b2*input-a2*output;
            if(std::abs(z1[channel])<1e-22)z1[channel]=0;
            if(std::abs(z2[channel])<1e-22)z2[channel]=0;
            return std::isfinite(output)?float(output):0;
        }
        void configure(int band,float db,double sampleRate) noexcept {
            constexpr double pi=3.14159265358979323846,frequencies[]={180,1000,5000};
            const double omega=2*pi*std::min(frequencies[band],sampleRate*.42)/sampleRate;
            const double c=std::cos(omega),s=std::sin(omega),A=std::pow(10.,db/40.);
            const double plus=A+1,minus=A-1,beta=std::sqrt(2*A)*s;double a0;
            if(band==0) {
                b0=A*(plus-minus*c+beta);b1=2*A*(minus-plus*c);b2=A*(plus-minus*c-beta);
                a0=plus+minus*c+beta;a1=-2*(minus+plus*c);a2=plus+minus*c-beta;
            } else if(band==1) {
                const double alpha=s/(2*.8);b0=1+alpha*A;b1=-2*c;b2=1-alpha*A;
                a0=1+alpha/A;a1=-2*c;a2=1-alpha/A;
            } else {
                b0=A*(plus+minus*c+beta);b1=-2*A*(minus+plus*c);b2=A*(plus+minus*c-beta);
                a0=plus-minus*c+beta;a1=2*(minus-plus*c);a2=plus-minus*c-beta;
            }
            b0/=a0;b1/=a0;b2/=a0;a1/=a0;a2/=a0;
        }
    };
    std::array<Biquad,3> filters{};double rate=48000;
public:
    void prepare(double sampleRate,int) override {rate=sampleRate>0?sampleRate:48000;reset(KernelResetReason::Reconfigure);}
    void configure(const KernelConfig& config) override {
        filters[0].configure(0,config.value(EQLow,0),rate);
        filters[1].configure(1,config.value(EQMid,0),rate);
        filters[2].configure(2,config.value(EQHigh,0),rate);
    }
    void reset(KernelResetReason) noexcept override {for(auto& filter:filters){filter.z1={};filter.z2={};}}
    void process(KernelContext& context) noexcept override {
        auto* output=context.output("audio");if(!output)return;const auto* input=context.input("audio");
        for(int channel=0;channel<2;++channel) {
            auto* out=channel?output->right:output->left;if(!out)continue;
            const float* in=input?(channel?input->right:input->left):nullptr;
            for(int frame=0;frame<context.frames;++frame) {
                float value=in?in[frame]:0;
                if(!context.bypassed)for(auto& filter:filters)value=filter.tick(value,channel);
                out[frame]=value;
            }
        }
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
