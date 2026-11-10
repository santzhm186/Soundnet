#include "Node.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace tri;
using namespace tri::nodes::oscillator;
struct Fixture {
    Kernel kernel;KernelConfig config;std::array<float,1025> left{},right{};
    KernelInput input{};KernelOutput output{};KernelContext context{};
    Fixture(){for(const auto& p:parameterSpecs())config.parameters[p.first]=p.second.info->def;
        config.parameters[Attack]=.001f;config.parameters[Release]=.005f;
        input.id="notes";output.id="audio";output.left=left.data();output.right=right.data();
        context.frames=1024;context.sampleRate=48000;context.inputs=&input;context.inputCount=1;context.outputs=&output;context.outputCount=1;
        kernel.prepare(48000,1024);kernel.configure(config);}
    void run(const KernelMidiEvent* events=nullptr,size_t count=0,int frames=1024){input.midi=events;input.midiCount=count;context.frames=frames;kernel.process(context);}
    double energy(int first=0,int last=1024){double e=0;for(int i=first;i<last;++i){assert(std::isfinite(left[i]));e+=left[i]*left[i];}return e;}
};
int main(){
    Fixture f;KernelMidiEvent on{17,0x90,69,100};f.left.fill(42);f.run(&on,1);assert(f.energy(0,17)==0);assert(f.energy(17,1024)>0);assert(f.left[1024]==42);
    KernelMidiEvent wrongOff{0,0x81,69,0};f.run(&wrongOff,1);assert(f.energy()>0);
    KernelMidiEvent off{0,0x80,69,0};f.run(&off,1);assert(f.energy(300,1024)==0);
    for(int shape=0;shape<4;++shape){f.kernel.reset(KernelResetReason::Stop);f.config.parameters[Wave1]=float(shape);f.kernel.configure(f.config);on.frame=0;f.run(&on,1);assert(f.energy()>0);}
    f.kernel.reset(KernelResetReason::Stop);KernelMidiEvent held[]={{0,0x90,60,100},{1,0xb0,64,127},{2,0x80,60,0}};f.run(held,3);assert(f.energy(300,1024)>0);
    KernelMidiEvent pedal{0,0xb0,64,0};f.run(&pedal,1);assert(f.energy(300,1024)==0);
    f.run(&on,1);KernelMidiEvent panic{0,0xb0,120,0};f.run(&panic,1);assert(f.energy()==0);
    Fixture isolated;isolated.run();assert(isolated.energy()==0);f.kernel.reset(KernelResetReason::Seek);f.run();assert(f.energy()==0);
    std::cout<<"PASS: oscillator waveforms, envelope, MIDI channels, sustain, reset and isolation\n";
}
