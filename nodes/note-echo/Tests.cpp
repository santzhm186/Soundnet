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
using namespace tri::nodes::note_echo;
int main(){Kernel k;Harness h;k.prepare(1000,128);KernelConfig c;c.parameters[EchoTime]=.05f;c.parameters[EchoRepeats]=2;c.parameters[EchoFeedback]=.5f;c.parameters[EchoPitch]=12;k.configure(c);
KernelMidiEvent in[]={{0,0x92,60,100},{1,0x82,60,0}};h.feed(k,in,2);assert(h.output.midiCount==4&&h.events[2].a==72&&h.events[2].b==50&&h.events[2].frame==50&&h.events[3].a==84&&h.events[3].frame==100);
h.feed(k,nullptr,0);assert(h.output.midiCount==2&&h.events[0].frame==72&&h.events[1].frame==122&&h.events[1].status==0x82);
h.feed(k,in,2,60);k.reset(KernelResetReason::Stop);h.feed(k,nullptr,0);assert(h.output.midiCount==0);h.feed(k,nullptr,0);assert(h.output.midiCount==0);c.parameters[EchoPitch]=0;k.configure(c);h.feed(k,in,1,60);assert(h.output.midiCount==3&&h.events[1].status==0x82&&h.events[2].status==0x92&&h.events[2].frame==50);puts("PASS note-echo: delayed pitches, decay, cross-block timing and cancellation");}
