#include "Node.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace tri;
using namespace tri::nodes::noise_gate;
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
    Fixture f;f.input(.01f);f.config.parameters[GateThreshold]=-20;f.config.parameters[GateRange]=1;f.config.parameters[GateRelease]=.01f;f.config.parameters[GateAttack]=.001f;f.apply();for(int i=0;i<100;++i)f.run();assert(f.energy()<1e-10);
    f.input(1);for(int i=0;i<20;++i)f.run();assert(f.outL[255]>.999f);
    f.context.bypassed=true;f.input(.01f);f.run();assert(f.outL[0]==.01f);
    {Fixture boundary;boundary.input(.2f);boundary.outL.fill(42);boundary.outR.fill(42);boundary.run(7);assert(boundary.outL[7]==42);assert(boundary.outR[7]==42);boundary.reset();boundary.context.inputs=nullptr;boundary.context.inputCount=0;boundary.run(1);}

    std::cout<<"PASS: noise-gate independent kernel behavior\n";
}
