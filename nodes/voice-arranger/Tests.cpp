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
using namespace tri::nodes::voice_arranger;
int main(){Kernel k;k.prepare(1000,128);k.configure({});KernelMidiEvent in[]={{3,0x91,60,90},{3,0x91,64,95},{3,0x91,67,100}};KernelInput input;input.id="notes";input.midi=in;input.midiCount=3;
KernelMidiEvent data[3][32]{};KernelOutput out[3];const char* names[]={"high","middle","low"};for(int v=0;v<3;++v){out[v].id=names[v];out[v].midi=data[v];out[v].midiCapacity=32;}
KernelContext c;c.frames=7;c.inputs=&input;c.inputCount=1;c.outputs=out;c.outputCount=3;k.process(c);
assert(out[0].midiCount==1&&data[0][0].a==79&&data[0][0].status==0x91);assert(data[1][0].a==64&&data[2][0].a==48);
k.reset(KernelResetReason::Stop);input.midiCount=0;for(auto& o:out)o.midiCount=0;k.process(c);for(int v=0;v<3;++v)assert(out[v].midiCount==0);
KernelConfig invalid;invalid.parameters[ArrHighMin]=100;invalid.parameters[ArrHighMax]=50;std::string why;assert(!validate(invalid,why));puts("PASS voice-arranger: three independent ports, range folding, reset, validation");}
