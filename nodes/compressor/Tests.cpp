#include "Node.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace tri;
using namespace tri::nodes::compressor;
struct Fixture {
    Kernel kernel;KernelConfig config;
    std::array<float,2049> left{},right{},side{},outL{},outR{};
    std::array<KernelInput,2> inputs{};KernelOutput output{};KernelContext context{};
    Fixture() {
        for(const auto& p:parameterSpecs())config.parameters[p.first]=p.second.info->def;
        inputs[0].id="audio";inputs[0].left=left.data();inputs[0].right=right.data();
        inputs[1].id="sidechain";
        output.id="audio";output.left=outL.data();output.right=outR.data();
        context.sampleRate=48000;context.frames=256;context.inputs=inputs.data();context.inputCount=1;
        context.outputs=&output;context.outputCount=1;
        kernel.prepare(48000,64);apply();
    }
    void apply(){kernel.configure(config);}
    void reset(){kernel.reset(KernelResetReason::Stop);}
    void run(int n=256){context.frames=n;kernel.process(context);for(int i=0;i<n;++i){assert(std::isfinite(outL[i]));assert(std::isfinite(outR[i]));}}
    double energy(int n=256)const{double e=0;for(int i=0;i<n;++i)e+=outL[i]*outL[i]+outR[i]*outR[i];return e;}
    void input(float value){left.fill(value);right.fill(value);}
};
int main(){
    Fixture f;f.input(.8f);f.config.parameters[CompThreshold]=-30;f.config.parameters[CompRatio]=20;f.config.parameters[CompAttack]=.001f;f.apply();for(int i=0;i<20;++i)f.run();assert(f.outL[255]<.1f);
    f.config.parameters[CompMix]=0;f.apply();f.run();assert(f.outL[0]==.8f);
    f.config.parameters[CompMix]=1;f.apply();f.reset();f.input(.01f);f.side.fill(1);f.inputs[1].left=f.inputs[1].right=f.side.data();f.context.inputCount=2;for(int i=0;i<20;++i)f.run();assert(f.outL[255]<.002f);
    f.reset();f.context.inputCount=1;f.run();assert(std::abs(f.outL[255]-.01f)<1e-6f);
    {Fixture boundary;boundary.input(.2f);boundary.outL.fill(42);boundary.outR.fill(42);boundary.run(7);assert(boundary.outL[7]==42);assert(boundary.outR[7]==42);boundary.reset();boundary.context.inputs=nullptr;boundary.context.inputCount=0;boundary.run(1);}

    std::cout<<"PASS: compressor independent kernel behavior\n";
}
