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
using namespace tri::nodes::transpose;
static void checkClamp(int shift,uint8_t first,uint8_t second,uint8_t boundary){
    Kernel k;Harness h;k.prepare(1000,128);KernelConfig c;c.parameters[TransposeSemitones]=shift;k.configure(c);
    KernelMidiEvent on[]={{0,0x93,first,100},{1,0x93,second,90},{2,0x94,second,80}};h.feed(k,on,3);
    assert(h.output.midiCount==2&&h.events[0].a==boundary&&h.events[0].status==0x93&&h.events[1].status==0x94);
    KernelMidiEvent off[]={{0,0x83,first,0},{1,0x84,second,0},{2,0x83,second,17}};h.feed(k,off,3);
    assert(h.output.midiCount==2&&h.events[0].status==0x84&&h.events[1].status==0x83&&h.events[1].b==17);
    h.feed(k,on,2);h.context.bypassed=true;h.feed(k,nullptr,0);
    assert(h.output.midiCount==3&&h.events[0].status==0x83);
    k.reset(KernelResetReason::Stop);h.feed(k,nullptr,0);assert(h.output.midiCount==0);
}
static void overflowRecovery(){Kernel k;Harness h;k.prepare(1000,128);k.configure({});KernelMidiEvent on{0,0x90,60,100};h.feed(k,&on,1);h.output.midiCapacity=0;KernelMidiEvent off{0,0x80,60,0};h.feed(k,&off,1);assert(h.output.overflow);h.output.midiCapacity=h.events.size();h.feed(k,nullptr,0);assert(h.output.midiCount==1&&h.events[0].status==0x80);}
int main(){overflowRecovery();checkClamp(24,103,127,127);checkClamp(-24,24,0,0);puts("PASS transpose: clamped overlap, channels, bypass and reset");}
