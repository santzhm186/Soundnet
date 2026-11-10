#include "Node.hpp"
#include "ControllerLane.hpp"
#include "../../platform/runtime/MidiInputCapture.hpp"
#include <cassert>
#include <cstdio>


static void kernelPlayback() {
    using namespace tri;using namespace tri::nodes::midi_clip;
    Kernel kernel;kernel.prepare(1000,128);KernelConfig config;
    auto clip=std::make_shared<MidiClip>();clip->lengthBeats=.5;
    clip->events={{0,0x92,60,100},{.1,0xb2,64,127},{.5,0x82,60,23}};
    config.clip=clip;config.parameters[ClockBPM]=120;config.parameters[ClipLoop]=0;kernel.configure(config);
    KernelMidiEvent midi[64]{};KernelTriggerEvent cycle[8]{};KernelOutput outputs[2];
    outputs[0].id="notes";outputs[0].midi=midi;outputs[0].midiCapacity=64;
    outputs[1].id="cycle";outputs[1].triggers=cycle;outputs[1].triggerCapacity=8;
    KernelTriggerEvent start{3,true};KernelInput trigger;trigger.id="trigger";trigger.triggers=&start;trigger.triggerCount=1;
    KernelContext context;context.playing=true;context.frames=128;context.inputs=&trigger;context.inputCount=1;context.outputs=outputs;context.outputCount=2;
    kernel.process(context);assert(outputs[0].midiCount==2&&midi[0].frame==3&&midi[0].status==0x92&&midi[1].frame==53);
    outputs[0].midiCount=0;trigger.triggerCount=0;kernel.process(context);
    assert(outputs[1].triggerCount==1&&cycle[0].frame==125);
    assert(outputs[0].midiCount==2&&midi[0].status==0x82&&midi[0].b==23&&midi[1].status==0xb2&&midi[1].a==64&&midi[1].b==0);
    outputs[0].midiCount=0;outputs[1].triggerCount=0;kernel.process(context);assert(outputs[0].midiCount==0&&outputs[1].triggerCount==0);
    kernel.reset(KernelResetReason::Stop);
}

static void loopBoundaryAndSeek() {
    using namespace tri;using namespace tri::nodes::midi_clip;
    Kernel kernel;kernel.prepare(48000,128);KernelConfig config;auto clip=std::make_shared<MidiClip>();
    clip->lengthBeats=.25;clip->events={{0,0x90,60,100},{.2,0x80,60,0}};
    config.clip=clip;config.parameters[ClockBPM]=120;config.parameters[ClipRun]=1;kernel.configure(config);
    KernelMidiEvent midi[64]{};KernelTriggerEvent cycles[8]{};KernelOutput outputs[2];
    outputs[0].id="notes";outputs[0].midi=midi;outputs[0].midiCapacity=64;
    outputs[1].id="cycle";outputs[1].triggers=cycles;outputs[1].triggerCapacity=8;
    KernelContext context;context.playing=true;context.frames=128;context.outputs=outputs;context.outputCount=2;
    int completions=0,attacks=0;
    for(int b=0;b<94;++b){outputs[0].midiCount=outputs[1].triggerCount=0;kernel.process(context);context.framePosition+=128;
        completions+=outputs[1].triggerCount;for(size_t i=0;i<outputs[0].midiCount;++i)attacks+=(midi[i].status==0x90&&midi[i].b);}
    assert(completions==2&&attacks==3);
    kernel.reset(KernelResetReason::Seek);context.framePosition=9000;outputs[0].midiCount=outputs[1].triggerCount=0;kernel.process(context);
    assert(outputs[0].midiCount==1&&midi[0].status==0x90&&midi[0].a==60&&midi[0].frame==0);
    kernel.reset(KernelResetReason::Stop);outputs[0].midiCount=outputs[1].triggerCount=0;kernel.process(context);assert(outputs[0].midiCount==0);
}

static void hostPauseResumeAndSeek() {
    using namespace tri;using namespace tri::nodes::midi_clip;
    Kernel kernel;kernel.prepare(1000,128);KernelConfig config;
    auto clip=std::make_shared<MidiClip>();clip->lengthBeats=.5;
    clip->events={{0,0x92,60,100},{.02,0xb2,64,127},{.04,0xe2,0,96},
                  {.2,0x82,60,23},{.24,0xb2,64,0}};
    config.clip=clip;config.parameters[ClockBPM]=120;config.parameters[ClipLoop]=0;
    config.parameters[ClipRun]=1;kernel.configure(config);
    KernelMidiEvent midi[64]{};KernelTriggerEvent cycles[8]{};KernelOutput outputs[2];
    outputs[0].id="notes";outputs[0].midi=midi;outputs[0].midiCapacity=64;
    outputs[1].id="cycle";outputs[1].triggers=cycles;outputs[1].triggerCapacity=8;
    KernelTriggerEvent pulse{};KernelInput trigger;trigger.id="trigger";trigger.triggers=&pulse;
    KernelContext context;context.inputs=&trigger;context.inputCount=1;context.outputs=outputs;context.outputCount=2;
    auto render=[&](bool playing,int frames) {
        context.playing=playing;context.frames=frames;
        outputs[0].midiCount=outputs[1].triggerCount=0;
        kernel.process(context);if(playing)context.framePosition+=frames;
        trigger.triggerCount=0;assert(!outputs[0].overflow&&!outputs[1].overflow);
    };
    auto has=[&](uint8_t status,uint8_t a,uint8_t b,int frame) {
        for(size_t i=0;i<outputs[0].midiCount;++i)
            if(midi[i].status==status&&midi[i].a==a&&midi[i].b==b&&midi[i].frame==frame)return true;
        return false;
    };
    render(true,40);assert(outputs[0].midiCount==3&&has(0x92,60,100,0)&&has(0xb2,64,127,10)&&has(0xe2,0,96,20));
    render(false,64);
    assert(outputs[0].midiCount==3&&has(0x82,60,0,0)&&has(0xb2,64,0,0)&&has(0xe2,0,64,0));
    for(int i=0;i<3;++i){render(false,128);assert(outputs[0].midiCount==0&&outputs[1].triggerCount==0);}
    assert(context.framePosition==40);
    render(true,37);
    assert(outputs[0].midiCount==3&&has(0x92,60,100,0)&&has(0xb2,64,127,0)&&has(0xe2,0,96,0));
    render(true,31);assert(outputs[0].midiCount==1&&has(0x82,60,23,23));

    // Seeking while paused updates the musical position without emitting a
    // chase. The first resumed block releases the note at the precise offset.
    render(false,11);assert(outputs[0].midiCount==2);
    kernel.reset(KernelResetReason::Seek);context.framePosition=50;
    render(false,128);assert(outputs[0].midiCount==0);
    render(false,31);assert(outputs[0].midiCount==0);
    render(true,64);
    assert(outputs[0].midiCount==4&&has(0x92,60,100,0)&&has(0x82,60,23,50));

    // Trigger-only clips can start/stop while the host is paused. Their run
    // intent survives both pause and seek even though clip.run is disabled.
    kernel.reset(KernelResetReason::Stop);config.parameters[ClipRun]=0;kernel.configure(config);
    pulse={7,true};trigger.triggerCount=1;render(false,64);assert(outputs[0].midiCount==0);
    render(false,77);assert(outputs[0].midiCount==0);
    render(true,40);assert(outputs[0].midiCount==3&&has(0x92,60,100,0)&&has(0xb2,64,127,10));
    render(false,128);assert(has(0x82,60,0,0));
    kernel.reset(KernelResetReason::Seek);context.framePosition=50;
    render(false,64);assert(outputs[0].midiCount==0);
    render(true,64);assert(outputs[0].midiCount==4&&has(0x92,60,100,0)&&has(0x82,60,23,50));
    pulse={9,false};trigger.triggerCount=1;render(false,64);
    render(true,64);assert(outputs[0].midiCount==0&&outputs[1].triggerCount==0);
}

int main() {
    kernelPlayback();
    loopBoundaryAndSeek();
    hostPauseResumeAndSeek();
    using namespace tri::nodes::midi_clip;
    for(int cc=0;cc<128;++cc) {
        ControllerLane lane{Lane::ControlChange,15,cc};
        auto event=lane.event(.25,127);
        assert(event.status==0xbf && event.data1==cc && lane.matches(event));
        assert(lane.value(event)==127);
    }
    ControllerLane bend{Lane::PitchBend,3};
    for(int value=-8192;value<8192;++value) {
        auto event=bend.event(0,value);
        assert(event.status==0xe3 && bend.value(event)==value);
    }
    for(auto kind:{Lane::ChannelPressure,Lane::PolyPressure,Lane::ProgramChange}) {
        ControllerLane lane{kind,4,60};
        const auto event=lane.event(.5,80);
        assert(lane.matches(event) && lane.value(event)==80 && event.beat==.5);
    }
    tri::MidiInputCapture capture(48000,120);
    const tri::KernelMidiEvent input[]={{0,0x91,60,87},{128,0xb1,11,72},{255,0x81,60,18}};
    capture.append(input,3,256);
    capture.stop();
    capture.append(input,3,256);
    auto clip=capture.clip();
    assert(!capture.overflowed()&&clip.events.size()==3);
    assert(clip.events[0].status==0x91&&clip.events[0].data2==87);
    assert(clip.events[1].status==0xb1&&clip.events[1].data1==11&&clip.events[1].data2==72);
    assert(clip.events[2].status==0x81&&clip.events[2].data2==18&&clip.events[2].beat>clip.events[1].beat);
    std::puts("PASS: MIDI Clip host pause/resume/seek, trigger intent, controller values, 14-bit bend and recorded note timing");
}
