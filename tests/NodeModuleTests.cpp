#include "NodeTestLookup.hpp"
#include "../src/GraphSpec.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <stdexcept>
using namespace tri;

static GraphNode graphNode(uint64_t id,const char* key) {
    GraphNode node;node.id=id;node.key=key;return node;
}
int main() {
    assert(registryProblems().empty());
    const auto* gain=nodeSpecForKey("soundnet.gain");
    const auto* split=nodeSpecForKey("soundnet.midi-split");
    const auto* time=nodeSpecForKey("soundnet.timecode");
    const auto* settings=nodeSpecForKey("soundnet.project-settings");
    assert(gain&&split&&time&&settings);
    assert(gain->kernelFactory&&split->kernelFactory&&time->kernelFactory&&settings->kernelFactory);
    assert(time->environmentRole==EnvironmentRole::Timecode);
    assert(settings->environmentRole==EnvironmentRole::ProjectSettings);
    for(const char* retired:{"soundnet.clock","soundnet.daw-clock","soundnet.legacy-osc2",
        "soundnet.legacy-osc3","soundnet.disconnected","soundnet.master-output",
        "soundnet.daw-audio-input","soundnet.daw-audio-output"})
        assert(!nodeSpecForKey(retired));
    for(int type:{ntest::type("arp"),ntest::type("midi-clip"),ntest::type("progression"),ntest::type("velocity")}) {
        assert(nodeInputByRole(type,RoleClock)==-1);
        assert(nodeAcceptsParam(type,nodeParamIdForKey(type,"clock.bpm")));
    }
    assert(nodeParamIdForKey(gain->type,"gain")==nodes::gain::amountID);
    assert(nodeParamIdForKey(split->type,"gain")==-1);
    assert(parameterID("example.a","amount")!=parameterID("example.b","amount"));
    // Registry rejects a duplicate at insertion, before a map can hide it.
    std::map<int,ParamSpec> duplicate;
    registerParameter(duplicate,1,{&nodes::gain::amountInfo,"amount",false});
    bool rejected=false;
    try {registerParameter(duplicate,1,{&nodes::gain::amountInfo,"other",false});}
    catch(const std::logic_error&) {rejected=true;}
    assert(rejected);
    // A descriptor cannot borrow another module's parameter merely because
    // that module was registered earlier in the catalog.
    {
        auto gainModule=nodes::gain::module();
        auto transposeModule=nodes::transpose::module();
        transposeModule.spec.params.push_back(gainModule.spec.params.front());
        bool rejectedForeign=false;
        try {makeNodeCatalog({gainModule,transposeModule});}
        catch(const std::logic_error&) {rejectedForeign=true;}
        assert(rejectedForeign);
    }
    // Stereo, partial processing blocks, silence and bypass through public SDK.
    {
        auto kernel=gain->kernelFactory();kernel->prepare(48000,128);
        KernelConfig config;config.parameters[nodes::gain::amountID]=2;
        kernel->configure(config);
        float left[]={.1f,.2f,.3f},right[]={-.1f,-.2f,-.3f},outL[3]{},outR[3]{};
        KernelInput input;input.id="audio";input.left=left;input.right=right;
        KernelOutput output;output.id="audio";output.left=outL;output.right=outR;
        KernelContext context;context.frames=3;context.inputs=&input;context.inputCount=1;
        context.outputs=&output;context.outputCount=1;
        kernel->process(context);
        for(int i=0;i<3;++i){assert(outL[i]==left[i]*2);assert(outR[i]==right[i]*2);}
        context.bypassed=true;kernel->process(context);
        assert(outL[2]==left[2]&&outR[2]==right[2]);
        context.inputs=nullptr;context.inputCount=0;kernel->process(context);
        assert(outL[2]==0&&outR[2]==0);
    }
    // Output port identity is independent of display/array order. A held note
    // stays paired when the split parameter changes between blocks.
    {
        auto kernel=split->kernelFactory();kernel->prepare(48000,128);
        KernelConfig config;config.parameters[nodes::midi_split::splitID]=60;
        kernel->configure(config);
        KernelMidiEvent on[]={{2,0x90,48,100},{7,0x91,72,90}};
        std::array<KernelMidiEvent,8> highBuffer{},lowBuffer{};
        KernelInput input;input.id="notes";input.midi=on;input.midiCount=2;
        KernelOutput outputs[2];
        outputs[0].id="high";outputs[0].midi=highBuffer.data();outputs[0].midiCapacity=8;
        outputs[1].id="low";outputs[1].midi=lowBuffer.data();outputs[1].midiCapacity=8;
        KernelContext context;context.frames=32;context.inputs=&input;context.inputCount=1;
        context.outputs=outputs;context.outputCount=2;
        kernel->process(context);
        assert(outputs[0].midiCount==1&&outputs[1].midiCount==1);
        assert(highBuffer[0].a==72&&highBuffer[0].frame==7);
        config.parameters[nodes::midi_split::splitID]=80;kernel->configure(config);
        KernelMidiEvent off={3,0x81,72,0};input.midi=&off;input.midiCount=1;
        outputs[0].midiCount=outputs[1].midiCount=0;
        kernel->process(context);
        assert(outputs[0].midiCount==1&&outputs[1].midiCount==0);
        assert(highBuffer[0].status==0x81);
        outputs[0].midiCapacity=0;outputs[0].midiCount=0;
        assert(!outputs[0].emitMidi(off)&&outputs[0].overflow);
    }
    // Timecode publishes positions without importing a global BPM.
    {
        auto kernel=time->kernelFactory();kernel->prepare(44100,128);
        KernelTimecode clock;KernelOutput output;output.id="time";output.timecode=&clock;
        KernelContext context;context.sampleRate=44100;context.frames=17;
        context.framePosition=88200;context.timeGeneration=9;context.playing=true;
        context.outputs=&output;context.outputCount=1;
        kernel->process(context);
        assert(clock.framePosition==88200&&clock.generation==9&&clock.sampleRate==44100&&clock.playing);
    }
    // All graph entry points execute the module's cross-parameter validator.
    {
        GraphSpec graph;GraphNode node=graphNode(1,"soundnet.yue2");
        node.parameters={{nodes::yue2::startID,.9f},{nodes::yue2::endID,.1f}};graph.nodes={node};
        CompiledGraph compiled;assert(!compileGraph(graph,compiled));
        graph.nodes={graphNode(1,"soundnet.project-settings")};
        graph.nodes[0].parameters[nodes::project_settings::bitsID]=20;
        assert(!compileGraph(graph,compiled));
        graph.nodes[0].parameters[nodes::project_settings::bitsID]=24;
        assert(compileGraph(graph,compiled));
        graph.nodes[0].type=ntest::type("oscillator");assert(!compileGraph(graph,compiled));
        graph.nodes[0].type=0;graph.nodes[0].nodeVersion=99;
        assert(!compileGraph(graph,compiled));
    }
    // Full projects require their environment, while isolated node tests may
    // compile a subgraph. Duplicate environment instances are never ambiguous.
    {
        GraphSpec graph;graph.environmentRequired=true;CompiledGraph plan;
        assert(!compileGraph(graph,plan));
        graph.nodes={graphNode(1,"soundnet.timecode"),graphNode(2,"soundnet.project-settings")};
        assert(compileGraph(graph,plan));
        graph.environmentRequired=false;
        graph.nodes.push_back(graphNode(3,"soundnet.timecode"));
        assert(!compileGraph(graph,plan));
    }
    // A task-like kernel can publish independent started/completed trigger
    // ports, each carrying its own block offset, without any central type ID.
    {
        struct TriggerFixture final : NodeKernel {
            void prepare(double,int) override {}
            void configure(const KernelConfig&) override {}
            void reset(KernelResetReason) noexcept override {}
            void process(KernelContext& context) noexcept override {
                const auto* in=context.input("request");
                if(!in)return;
                for(size_t i=0;i<in->triggerCount;++i) {
                    const auto event=in->triggers[i];
                    auto* out=context.output(event.start?"started":"completed");
                    if(out)out->emitTrigger(event);
                }
            }
        } kernel;
        KernelTriggerEvent events[]={{3,true},{17,false}},started[2]{},completed[2]{};
        KernelInput input;input.id="request";input.triggers=events;input.triggerCount=2;
        KernelOutput outputs[2];
        outputs[0].id="completed";outputs[0].triggers=completed;outputs[0].triggerCapacity=2;
        outputs[1].id="started";outputs[1].triggers=started;outputs[1].triggerCapacity=2;
        KernelContext context;context.frames=32;context.inputs=&input;context.inputCount=1;
        context.outputs=outputs;context.outputCount=2;kernel.process(context);
        assert(outputs[0].triggerCount==1&&outputs[1].triggerCount==1);
        assert(completed[0].frame==17&&!completed[0].start&&started[0].frame==3&&started[0].start);
    }
    puts("PASS: independent module factories, scoped parameters, stereo/MIDI/timecode SDK and shared validation");
}
