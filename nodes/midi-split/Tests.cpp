#include "Kernel.hpp"
#include <cassert>
#include <cstdio>
int main() {
    tri::nodes::midi_split::Kernel kernel;kernel.prepare(48000,128);
    tri::KernelConfig config;config.parameters[tri::nodes::midi_split::splitID]=60;kernel.configure(config);
    tri::KernelMidiEvent messages[]={{1,0x92,72,100},{3,0x90,48,90}},lowBuffer[8]{},highBuffer[8]{};
    tri::KernelInput input;input.id="notes";input.midi=messages;input.midiCount=2;
    tri::KernelOutput output[2];
    output[0].id="high";output[0].midi=highBuffer;output[0].midiCapacity=8;
    output[1].id="low";output[1].midi=lowBuffer;output[1].midiCapacity=8;
    tri::KernelContext context;context.frames=32;context.inputs=&input;context.inputCount=1;
    context.outputs=output;context.outputCount=2;
    kernel.process(context);assert(output[0].midiCount==1&&output[1].midiCount==1);
    assert(highBuffer[0].frame==1&&highBuffer[0].status==0x92);
    config.parameters[tri::nodes::midi_split::splitID]=90;kernel.configure(config);
    tri::KernelMidiEvent off{2,0x82,72,0};input.midi=&off;input.midiCount=1;
    output[0].midiCount=output[1].midiCount=0;kernel.process(context);
    assert(output[0].midiCount==1&&output[1].midiCount==0&&highBuffer[0].status==0x82);
    puts("PASS: MIDI split standalone ports and note-off pairing across parameter edits");
}
