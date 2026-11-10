#include "Node.hpp"
#include <array>
#include <cassert>
#include <cstdio>
int main() {
    using namespace tri::nodes::midi_output;
    Kernel kernel;kernel.prepare(48000,64);
    tri::KernelMidiEvent source[]={{0,0x9f,60,90},{1,0xbf,64,127},{2,0xef,0,80},{3,0xcf,5,0},{4,0xdf,70,0},{5,0xaf,60,32},{6,0x8f,60,0},{7,0xf8,0,0}};
    std::array<tri::KernelMidiEvent,8> graphEvents{},hostEvents{};
    tri::KernelInput input;input.id="notes";input.midi=source;input.midiCount=8;
    tri::KernelOutput output;output.id="notes";output.midi=graphEvents.data();output.midiCapacity=8;
    tri::KernelOutput host;host.midi=hostEvents.data();host.midiCapacity=8;
    tri::KernelContext context;context.frames=8;context.inputs=&input;context.inputCount=1;
    context.outputs=&output;context.outputCount=1;context.hostMidi=&host;
    for(int channel=0;channel<=16;++channel) {
        tri::KernelConfig config;config.parameters[MidiOutChannel]=float(channel);kernel.configure(config);
        output.midiCount=host.midiCount=0;kernel.process(context);
        assert(output.midiCount==8&&host.midiCount==8);
        for(size_t i=0;i<8;++i) {
            const int expected=source[i].status<0xf0&&channel>0?(source[i].status&0xf0)|(channel-1):source[i].status;
            assert(graphEvents[i].status==expected&&hostEvents[i].status==expected);
            assert(hostEvents[i].frame==source[i].frame&&hostEvents[i].a==source[i].a&&hostEvents[i].b==source[i].b);
        }
    }
    output.midiCount=host.midiCount=0;host.midiCapacity=1;kernel.process(context);
    assert(host.midiCount==1&&host.overflow&&output.midiCount==8);
    context.outputCount=0;host.midiCount=0;host.midiCapacity=8;kernel.process(context);assert(host.midiCount==8);
    context.inputCount=0;host.midiCount=0;kernel.reset(tri::KernelResetReason::Stop);kernel.process(context);assert(host.midiCount==0);
    assert(source[0].status==0x9f&&source[7].status==0xf8);
    puts("PASS: MIDI output channel mapping, host/graph sinks, message identity and bounded overflow");
}
