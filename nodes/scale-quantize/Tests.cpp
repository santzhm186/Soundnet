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
using namespace tri::nodes::scale_quantize;
int main(){Kernel k;Harness h;k.prepare(1000,128);KernelConfig c;k.configure(c);
KernelMidiEvent on[]={{3,0x92,61,100},{4,0x92,60,90}};h.feed(k,on,2);assert(h.output.midiCount==1&&h.events[0].a==60&&h.events[0].frame==3);
KernelMidiEvent off[]={{7,0x82,61,0},{8,0x82,60,0}};h.feed(k,off,2);assert(h.output.midiCount==1&&h.events[0].frame==8&&h.events[0].a==60);
assert(quantizePitch(61,0,0,1)==62&&quantizePitch(61,0,0,2)==60);h.feed(k,on,1);c.parameters[QuantizeMode]=1;k.configure(c);h.feed(k,nullptr,0);assert(h.output.midiCount==2&&h.events[0].status==0x82&&h.events[1].a==62);
k.reset(KernelResetReason::Stop);h.feed(k,nullptr,0);assert(h.output.midiCount==0);puts("PASS scale-quantize: overlap, modes, held reconfiguration and reset");}
