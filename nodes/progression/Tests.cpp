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
using namespace tri::nodes::progression;
int main(){Kernel k;Harness h;k.prepare(1000,128);KernelConfig c;c.parameters[ProgressRun]=1;c.parameters[ClockBPM]=240;c.parameters[ProgressLength]=2;c.parameters[ProgressQuality1]=0;c.parameters[ProgressQuality2]=0;k.configure(c);
h.feed(k,nullptr,0,500);assert(h.output.midiCount==3&&h.events[0].a==60);h.feed(k,nullptr,0,501);assert(h.output.midiCount==6&&h.events[0].frame==500&&h.events[3].a==69);
KernelTriggerEvent stop{7,false};h.input.id="trigger";h.input.triggers=&stop;h.input.triggerCount=1;h.feed(k,nullptr,0,8);assert(h.output.midiCount==3&&h.events[0].frame==7);h.input.triggerCount=0;h.feed(k,nullptr,0);assert(h.output.midiCount==0);k.reset(KernelResetReason::Seek);h.context.framePosition=1000;h.feed(k,nullptr,0,1);assert(h.output.midiCount==3&&h.events[0].a==69);puts("PASS progression: local tempo, chord boundary, frame-accurate stop");}
