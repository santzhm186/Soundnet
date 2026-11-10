#include "Node.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace tri;
using namespace tri::nodes::sampler;
int main(){
    Kernel kernel;KernelConfig config;for(const auto& p:parameterSpecs())config.parameters[p.first]=p.second.info->def;
    auto audio=std::make_shared<AudioSample>();audio->rate=48000;audio->channels=1;audio->samples.assign(8192,1);config.audio=audio;
    config.parameters[SamplerAttack]=.001f;config.parameters[SamplerRelease]=.001f;config.parameters[SamplerGain]=1;config.parameters[SamplerLoop]=1;config.parameters[SamplerXfade]=0;
    kernel.prepare(48000,256);kernel.configure(config);
    std::array<float,257> left{},right{};left.fill(42);
    KernelInput input{};input.id="notes";KernelOutput output{};output.id="audio";output.left=left.data();output.right=right.data();
    KernelContext context{};context.frames=256;context.inputs=&input;context.inputCount=1;context.outputs=&output;context.outputCount=1;
    auto run=[&](const KernelMidiEvent* events,size_t count){input.midi=events;input.midiCount=count;kernel.process(context);};
    KernelMidiEvent events[]={{17,0x90,60,127},{18,0x91,60,127}};run(events,2);for(int i=0;i<17;++i)assert(left[i]==0);assert(left[255]>1.99f);assert(left[256]==42);
    KernelMidiEvent off{0,0x80,60,0};run(&off,1);assert(std::abs(left[255]-1)<1e-6);
    off.status=0x81;run(&off,1);assert(left[255]==0);
    config.parameters[SamplerStart]=.25f;config.parameters[SamplerEnd]=.75f;config.parameters[SamplerReverse]=1;config.parameters[SamplerXfade]=5;kernel.configure(config);run(events,2);assert(std::isfinite(left[255]));
    context.bypassed=true;run(nullptr,0);for(int i=0;i<256;++i)assert(left[i]==0);context.bypassed=false;run(nullptr,0);assert(left[255]==0);
    kernel.reset(KernelResetReason::Stop);run(nullptr,0);assert(left[255]==0);
    config.audio.reset();kernel.configure(config);run(events,2);assert(left[255]==0);
    std::string why;config.parameters[SamplerStart]=.9f;config.parameters[SamplerEnd]=.1f;assert(!validate(config,why));
    std::cout<<"PASS: sampler voices, channel releases, bounds, reverse, loop, bypass and missing resources\n";
}
