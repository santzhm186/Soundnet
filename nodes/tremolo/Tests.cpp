#include "Node.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace tri;
using namespace tri::nodes::tremolo;
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
    Fixture f;f.input(1);f.config.parameters[TremoloDepth]=1;f.config.parameters[TremoloMix]=1;f.config.parameters[TremoloStereo]=1;f.apply();f.run();assert(std::abs(f.outL[0]-.5f)<1e-6f);assert(f.outL[255]>f.outR[255]);
    Fixture sync,free;sync.input(1);free.input(1);sync.config.parameters[TremoloSync]=1;sync.config.parameters[ClockBPM]=120;sync.config.parameters[TremoloDivision]=0;free.config.parameters[TremoloRate]=2;sync.apply();free.apply();sync.run();free.run();for(int i=0;i<256;++i)assert(std::abs(sync.outL[i]-free.outL[i])<1e-6);
    f.reset();f.run(1);assert(std::abs(f.outL[0]-.5f)<1e-6f);
    {Fixture boundary;boundary.input(.2f);boundary.outL.fill(42);boundary.outR.fill(42);boundary.run(7);assert(boundary.outL[7]==42);assert(boundary.outR[7]==42);boundary.reset();boundary.context.inputs=nullptr;boundary.context.inputCount=0;boundary.run(1);}

    std::cout<<"PASS: tremolo independent kernel behavior\n";
}
