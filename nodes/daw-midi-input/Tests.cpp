#include "Node.hpp"
#include <array>
#include <cassert>
#include <cstdio>
int main() {
    tri::nodes::daw_midi_input::Kernel kernel;kernel.prepare(48000,256);kernel.configure({});
    std::array<tri::KernelMidiEvent,113> source{},destination{};
    size_t count=0;
    for(unsigned channel=0;channel<16;++channel)for(unsigned status=0x80;status<=0xe0;status+=0x10)
        source[count]={int(count%64),uint8_t(status|channel),60,90},++count;
    source[count++]={63,0xf8,0,0};
    tri::KernelOutput output;output.id="notes";output.midi=destination.data();output.midiCapacity=destination.size();
    tri::KernelContext context;context.frames=64;context.outputs=&output;context.outputCount=1;
    context.dawMidi=source.data();context.dawMidiCount=count;
    kernel.process(context);assert(output.midiCount==count&&!output.overflow);
    for(size_t i=0;i<count;++i)assert(destination[i].frame==source[i].frame&&destination[i].status==source[i].status&&
        destination[i].a==source[i].a&&destination[i].b==source[i].b);
    output.midiCount=0;output.midiCapacity=2;kernel.process(context);
    assert(output.midiCount==2&&output.overflow); // bounded output never writes past capacity
    output.midiCount=0;output.overflow=false;context.dawMidiCount=0;context.dawMidi=nullptr;
    kernel.reset(tri::KernelResetReason::Stop);kernel.process(context);assert(output.midiCount==0);
    context.outputCount=0;kernel.process(context);
    assert(tri::nodes::daw_midi_input::module().spec.kernelFactory);
    puts("PASS: daw-midi-input all channels/messages, exact frame offsets, bounded overflow and empty input");
}
