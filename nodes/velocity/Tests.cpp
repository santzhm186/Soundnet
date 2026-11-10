#include "Node.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>
#include <array>
using namespace tri;
struct Harness {
    std::array<KernelMidiEvent,8192> events{};
    KernelInput input;KernelOutput output;KernelContext context;
    Harness(){input.id="notes";output.id="notes";output.midi=events.data();output.midiCapacity=events.size();context.frames=128;context.sampleRate=1000;context.inputs=&input;context.inputCount=1;context.outputs=&output;context.outputCount=1;}
    void feed(NodeKernel& kernel,const KernelMidiEvent* e,size_t n,int frames=128){input.midi=e;input.midiCount=n;context.frames=frames;output.midiCount=0;output.overflow=false;kernel.process(context);context.framePosition+=frames;}
};
using namespace tri::nodes::velocity;
int main(){Kernel k;Harness h;k.prepare(1000,128);KernelConfig c;c.parameters[ClockBPM]=120;c.parameters[VelocityMin]=20;c.parameters[VelocityMax]=100;c.parameters[VelocityRate]=1;c.parameters[VelocityShape]=1;k.configure(c);
KernelMidiEvent on[]={{0,0x92,60,90},{125,0x92,64,90}};h.feed(k,on,2,128);assert(h.output.midiCount==2&&h.events[0].b==20&&h.events[1].b==40&&h.events[1].frame==125);
h.context.bypassed=true;KernelMidiEvent pass{2,0x93,70,77};h.feed(k,&pass,1,3);assert(h.events[0].b==77);k.reset(KernelResetReason::Stop);h.feed(k,nullptr,0);assert(h.output.midiCount==0);puts("PASS velocity: public frame time, node tempo, bypass and reset");}
