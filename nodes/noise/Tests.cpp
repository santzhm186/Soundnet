#include "Node.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace tri;
using namespace tri::nodes::noise;
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
    Fixture f;f.run(31);assert(f.energy(31)>0);const float first=f.outL[0];f.reset();f.run(31);assert(f.outL[0]==first);
    Fixture other;other.run(31);assert(other.outL==f.outL);
    f.config.parameters[NoiseLevel]=0;f.apply();f.run();assert(f.energy()==0);
    f.config.parameters[NoiseLevel]=1;f.config.parameters[NoiseType]=1;f.apply();f.run();assert(f.energy()>0);
    f.context.bypassed=true;f.run();assert(f.energy()==0);
    {Fixture boundary;boundary.input(.2f);boundary.outL.fill(42);boundary.outR.fill(42);boundary.run(7);assert(boundary.outL[7]==42);assert(boundary.outR[7]==42);boundary.reset();boundary.context.inputs=nullptr;boundary.context.inputCount=0;boundary.run(1);}

    std::cout<<"PASS: noise independent kernel behavior\n";
}
