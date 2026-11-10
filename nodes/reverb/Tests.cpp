#include "Node.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace tri;
using namespace tri::nodes::reverb;
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
    Fixture f;f.input(.4f);f.config.parameters[ReverbMix]=0;f.apply();for(int i=0;i<40;++i)f.run();for(int i=0;i<256;++i)assert(f.outL[i]==.4f);
    f.config.parameters[ReverbMix]=1;f.apply();f.reset();f.input(0);f.left[0]=f.right[0]=1;f.run();f.input(0);double tail=0;for(int i=0;i<40;++i){f.run();tail+=f.energy();}assert(tail>0);
    f.reset();f.run();assert(f.energy()==0);
    {Fixture boundary;boundary.input(.2f);boundary.outL.fill(42);boundary.outR.fill(42);boundary.run(7);assert(boundary.outL[7]==42);assert(boundary.outR[7]==42);boundary.reset();boundary.context.inputs=nullptr;boundary.context.inputCount=0;boundary.run(1);}

    std::cout<<"PASS: reverb independent kernel behavior\n";
}
