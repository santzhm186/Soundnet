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
using namespace tri::nodes::chord;
int main(){Kernel k;Harness h;k.prepare(1000,128);KernelConfig c;c.parameters[ChordQuality]=0;k.configure(c);
KernelMidiEvent on[]={{3,0x91,60,100},{3,0x91,64,90}};h.feed(k,on,2);assert(h.output.midiCount==5);for(size_t i=0;i<h.output.midiCount;++i)assert(h.events[i].status==0x91&&h.events[i].frame==3);
KernelMidiEvent off{7,0x81,60,0};h.feed(k,&off,1);assert(h.output.midiCount==2);for(size_t i=0;i<h.output.midiCount;++i)assert(h.events[i].a!=64);
k.reset(KernelResetReason::Stop);h.feed(k,nullptr,0);assert(h.output.midiCount==0);c.parameters[ChordRun]=1;c.parameters[ChordRoot]=48;k.configure(c);h.feed(k,nullptr,0);assert(h.output.midiCount==3&&h.events[0].a==48);puts("PASS chord: expansion, overlapping contributors, channels and generator");}
