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
using namespace tri::nodes::channel_filter;
int main(){Kernel k;Harness h;k.prepare(1000,128);KernelConfig c;c.parameters[ChannelFilter]=2;k.configure(c);
KernelMidiEvent on[]={{0,0x90,60,90},{1,0x91,64,100},{2,0xb0,11,70},{3,0xb1,11,80}};h.feed(k,on,4);assert(h.output.midiCount==2&&h.events[0].status==0x91&&h.events[1].status==0xb1);
c.parameters[ChannelFilter]=1;k.configure(c);h.feed(k,nullptr,0);assert(h.output.midiCount==2&&h.events[0].status==0x81&&h.events[1].status==0x90);
h.context.inputs=nullptr;h.context.inputCount=0;h.feed(k,nullptr,0);assert(h.output.midiCount==1&&h.events[0].status==0x80);puts("PASS channel-filter: notes, controls, channel switch and disconnect");}
