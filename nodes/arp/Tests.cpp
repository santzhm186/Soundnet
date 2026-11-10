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
using namespace tri::nodes::arp;
int main(){Kernel k;Harness h;k.prepare(1000,128);KernelConfig c;c.parameters[ArpOn]=1;c.parameters[Pattern]=4;c.parameters[ClockBPM]=240;c.parameters[Gate]=.5;k.configure(c);
KernelMidiEvent in[]={{0,0x92,60,100},{0,0x92,64,90}};h.feed(k,in,2);assert(h.output.midiCount>=4&&h.events[0].a==60&&h.events[0].status==0x92&&h.events[1].status==0x82&&h.events[2].a==64);
k.reset(KernelResetReason::Stop);h.feed(k,nullptr,0);for(size_t i=0;i<h.output.midiCount;++i)assert((h.events[i].status&0xf0)==0x80);
c.parameters[ArpOn]=0;k.configure(c);h.feed(k,in,2);assert(h.events[h.output.midiCount-1].a==64);puts("PASS arp: rhythm, channel pairing, reset and transparent disabled mode");}
