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
using namespace tri::nodes::note_model;
int main(){Kernel k;Harness h;k.prepare(1000,128);KernelConfig c;auto model=std::make_shared<NoteModel>();model->pitches[60]=72;model->pitches[61]=72;c.model=model;k.configure(c);
KernelMidiEvent on[]={{3,0x95,60,100},{4,0x95,61,90}};h.feed(k,on,2);assert(h.output.midiCount==1&&h.events[0].a==72);
KernelMidiEvent off[]={{1,0x85,60,0},{2,0x85,61,0}};h.feed(k,off,2);assert(h.output.midiCount==1&&h.events[0].frame==2&&h.events[0].status==0x85);
c.model.reset();k.configure(c);h.feed(k,on,1);assert(h.events[h.output.midiCount-1].a==60);k.reset(KernelResetReason::Stop);h.feed(k,nullptr,0);assert(h.output.midiCount==0);puts("PASS note-model: immutable resource mapping, overlap and fallback");}
