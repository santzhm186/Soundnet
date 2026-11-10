#include "Node.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace tri;
using namespace tri::nodes::audio_clip;
int main(){
    Kernel kernel;KernelConfig config;for(const auto& p:parameterSpecs())config.parameters[p.first]=p.second.info->def;
    auto audio=std::make_shared<AudioSample>();audio->rate=48000;audio->channels=2;audio->samples.resize(128*2);
    for(int i=0;i<128;++i){audio->samples[size_t(i)*2]=float(i)/128;audio->samples[size_t(i)*2+1]=.5f;}
    config.audio=audio;config.parameters[AudioClipGain]=1;config.parameters[AudioClipFade]=0;config.parameters[AudioClipLoop]=1;config.parameters[AudioClipXfade]=0;
    kernel.prepare(48000,256);kernel.configure(config);std::array<float,257> left{},right{};left.fill(42);
    KernelInput input{};input.id="trigger";KernelOutput output{};output.id="audio";output.left=left.data();output.right=right.data();
    KernelContext context{};context.frames=256;context.inputs=&input;context.inputCount=1;context.outputs=&output;context.outputCount=1;
    auto run=[&](const KernelTriggerEvent* events,size_t count){input.triggers=events;input.triggerCount=count;kernel.process(context);};
    KernelTriggerEvent events[]={{17,true},{40,false},{60,true},{80,false}};run(events,4);
    for(int i=0;i<17;++i)assert(left[i]==0);assert(left[18]>0);assert(left[40]==0);assert(left[61]>0);assert(left[80]==0);assert(left[256]==42);assert(right[18]==.5f);
    config.parameters[AudioClipReverse]=1;kernel.configure(config);KernelTriggerEvent start{0,true};run(&start,1);assert(std::abs(left[0]-127.f/128)<1e-6);assert(left[1]<left[0]);assert(left[128]==left[0]);
    config.parameters[AudioClipReverse]=0;config.parameters[AudioClipPitch]=12;kernel.configure(config);run(&start,1);assert(std::abs(left[1]-2.f/128)<1e-6);
    config.parameters[AudioClipFade]=.001f;kernel.configure(config);run(&start,1);assert(left[0]==0);KernelTriggerEvent stop{0,false};run(&stop,1);assert(left[100]==0);
    kernel.reset(KernelResetReason::Stop);run(nullptr,0);assert(left[1]==0);
    std::string why;config.parameters[AudioClipStart]=1;assert(!validate(config,why));
    std::cout<<"PASS: audio clip frame-accurate triggers, stereo, reverse, pitch, loop, fades and reset\n";
}
