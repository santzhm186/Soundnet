#include "../src/Engine.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using namespace tri;

struct Sent {int frame;uint8_t status,note,velocity;};
static GraphNode node(InstanceID id,const char* key) {
    GraphNode result;result.id=id;result.key=key;result.nodeVersion=nodeSpecForKey(key)->nodeVersion;return result;
}
static void connect(GraphSpec& graph,InstanceID source,const char* sourcePort,InstanceID target,const char* targetPort) {
    GraphEdge edge;edge.source=source;edge.sourcePortId=sourcePort;edge.target=target;edge.portId=targetPort;
    graph.edges.push_back(edge);
}
static bool has(const std::vector<Sent>& events,uint8_t status,uint8_t pitch) {
    for(const auto& event:events)if(event.status==status&&event.note==pitch)return true;
    return false;
}
static void collect(Engine& engine,std::vector<Sent>& events) {
    engine.setMidiOutput(&events,[](void* out,int frame,uint8_t status,uint8_t pitch,uint8_t velocity) {
        static_cast<std::vector<Sent>*>(out)->push_back({frame,status,pitch,velocity});
    });
}
int main() {
    // The same graph receives device or host buffers. Gain is per instance,
    // and mono input and offline rendering both have defined fallback audio.
    {
        GraphSpec graph;
        graph.nodes={node(1,"soundnet.audio-input"),node(2,"soundnet.audio-output")};
        graph.nodes[0].parameters={{InputGain,.5f}};
        graph.nodes[1].parameters={{Master,1},{OutputWidth,1}};
        connect(graph,1,"audio",2,"audio");
        auto engine=std::make_unique<Engine>();
        engine->prepare(48000);
        assert(engine->setGraph(graph));
        const float inL[]{.25f,.5f,-.75f},inR[]{-.5f,.25f,1.f};
        float left[3]{},right[3]{};
        engine->render(inL,inR,left,right,3);
        assert(left[0]==.125f && right[0]==-.25f && right[2]==.5f);
        // The standalone device bridge uses its input buffers in place.
        float deviceL[]{.25f,.5f,-.75f},deviceR[]{-.5f,.25f,1.f};
        engine->render(deviceL,deviceR,deviceL,deviceR,3);
        assert(deviceL[0]==.125f && deviceR[0]==-.25f && deviceR[2]==.5f);
        engine->setHostTimecode(true,128);
        engine->render(inL,nullptr,left,right,3);
        assert(left[2]==-.375f && right[2]==-.375f);
        engine->render(nullptr,nullptr,left,right,3);
        assert(left[2]==0 && right[2]==0);
        graph.nodes[0].parameters[InputGain]=0;
        assert(engine->setGraph(graph));
        engine->render(inL,inR,left,right,3);
        assert(left[2]==0 && right[2]==0);
    }
    // Existing generators and outputs communicate with a separately registered
    // kernel using named audio ports. Tail blocks and stereo survive the bridge.
    {
        GraphSpec reference;
        reference.nodes={node(1,"soundnet.noise"),node(3,"soundnet.audio-output")};
        reference.nodes[0].parameters={{NoiseLevel,.3f}};reference.nodes[1].parameters={{Master,1},{OutputWidth,1}};
        connect(reference,1,"audio",3,"audio");
        GraphSpec graph=reference;graph.edges.clear();
        graph.nodes.push_back(node(2,"soundnet.gain"));
        graph.nodes.back().parameters={{nodes::gain::amountID,.25f}};
        connect(graph,1,"audio",2,"audio");connect(graph,2,"audio",3,"audio");
        auto dry=std::make_unique<Engine>(),wet=std::make_unique<Engine>();
        dry->prepare(48000);wet->prepare(48000);assert(dry->setGraph(reference)&&wet->setGraph(graph));
        float dryL[257]{},dryR[257]{},wetL[257]{},wetR[257]{};
        dry->render(nullptr,nullptr,dryL,dryR,257);wet->render(nullptr,nullptr,wetL,wetR,257);
        float peak=0;
        for(int i=0;i<257;++i) {
            assert(std::abs(wetL[i]-dryL[i]*.25f)<1e-6f);
            assert(std::abs(wetR[i]-dryR[i]*.25f)<1e-6f);
            peak=std::max(peak,std::abs(wetL[i]));
        }
        assert(peak>.01f);
        wet->setProcessingEnabled(false);wet->render(nullptr,nullptr,wetL,wetR,257);
        for(int i=0;i<257;++i)assert(wetL[i]==0&&wetR[i]==0);
        wet->setProcessingEnabled(true);wet->render(nullptr,nullptr,wetL,wetR,257);
        peak=0;for(float value:wetL)peak=std::max(peak,std::abs(value));assert(peak>.01f);
    }
    // Separate output port IDs must remain separate through the old MIDI sink.
    // Parameter rebuilds and lost inputs release the actual forced channel.
    {
        GraphSpec graph;
        graph.nodes={node(1,"soundnet.midi-input"),node(2,"soundnet.midi-split"),
                     node(3,"soundnet.midi-output"),node(4,"soundnet.midi-output")};
        graph.nodes[1].parameters={{nodes::midi_split::splitID,60}};
        graph.nodes[2].parameters={{MidiOutChannel,2}};
        graph.nodes[3].parameters={{MidiOutChannel,3}};
        connect(graph,1,"notes",2,"notes");connect(graph,2,"low",3,"notes");connect(graph,2,"high",4,"notes");
        auto engine=std::make_unique<Engine>();engine->prepare(48000);assert(engine->setGraph(graph));
        std::vector<Sent> events;events.reserve(512);collect(*engine,events);
        float l[128],r[128];
        engine->midi(0x90,48,100);engine->midi(0x90,72,90);engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x91,48)&&has(events,0x92,72));
        assert(!has(events,0x91,72)&&!has(events,0x92,48));
        events.clear();graph.nodes[1].parameters[nodes::midi_split::splitID]=80;
        assert(engine->setGraph(graph));engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x82,72)&&has(events,0x91,72));
        events.clear();graph.edges.erase(graph.edges.begin());
        assert(engine->setGraph(graph));engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x81,48)&&has(events,0x81,72));
        events.clear();engine->render(nullptr,nullptr,l,r,128);
        assert(events.empty());
        // Restoring the input recovers held source notes once, then global stop
        // releases both voices. Resume accepts fresh MIDI only.
        connect(graph,1,"notes",2,"notes");assert(engine->setGraph(graph));
        engine->render(nullptr,nullptr,l,r,128);assert(has(events,0x91,48));
        events.clear();engine->setProcessingEnabled(false);engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x81,48)&&has(events,0x81,72));
        events.clear();engine->setProcessingEnabled(true);engine->render(nullptr,nullptr,l,r,128);
        assert(!has(events,0x91,48)&&!has(events,0x91,72));
        engine->midi(0x90,84,100);engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x92,84));
    }
    // A fresh kernel may need to chase held notes before processing a full
    // upstream event block. The extra chase must not truncate its final release.
    {
        GraphSpec graph;
        graph.nodes={node(1,"soundnet.midi-input"),node(2,"soundnet.midi-split"),
                     node(3,"soundnet.midi-output")};
        graph.nodes[1].parameters={{nodes::midi_split::splitID,60}};
        connect(graph,1,"notes",2,"notes");connect(graph,2,"high",3,"notes");
        auto engine=std::make_unique<Engine>();engine->prepare(48000);assert(engine->setGraph(graph));
        std::vector<Sent> events;events.reserve(8192);collect(*engine,events);
        float l[128]{},r[128]{};
        engine->midi(0x90,48,100);engine->render(nullptr,nullptr,l,r,128);
        graph.nodes[1].parameters[nodes::midi_split::splitID]=61;assert(engine->setGraph(graph));
        for(int i=0;i<2048;++i){engine->midi(0x90,72,100);engine->midi(0x80,72,0);}
        engine->render(nullptr,nullptr,l,r,128);
        int attacks=0,releases=0;
        for(const auto& event:events)if(event.note==72) {
            if(event.status==0x90&&event.velocity)++attacks;
            if(event.status==0x80)++releases;
        }
        assert(attacks==2048&&releases==2048);events.clear();
        engine->render(nullptr,nullptr,l,r,128);assert(events.empty());
    }
    // Changing a sink's forced channel releases the channel actually used by
    // its note-on, then chases the same held input onto the new destination.
    {
        GraphSpec graph;
        graph.nodes={node(1,"soundnet.midi-input"),node(2,"soundnet.midi-output")};
        graph.nodes[1].parameters={{MidiOutChannel,2}};
        connect(graph,1,"notes",2,"notes");
        auto engine=std::make_unique<Engine>();engine->prepare(48000);assert(engine->setGraph(graph));
        std::vector<Sent> events;events.reserve(64);collect(*engine,events);
        float l[128]{},r[128]{};
        engine->midi(0x90,60,100);engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x91,60));events.clear();
        graph.nodes[1].parameters[MidiOutChannel]=3;assert(engine->setGraph(graph));
        engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x81,60)&&has(events,0x92,60));events.clear();
        // Several unconsumed edits must use the last rendered channel, not an
        // intermediate configuration that has never sent any note-on.
        graph.nodes[1].parameters[MidiOutChannel]=4;assert(engine->setGraph(graph));
        graph.nodes[1].parameters[MidiOutChannel]=0;assert(engine->setGraph(graph));
        engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x82,60)&&has(events,0x90,60));
        assert(!has(events,0x83,60));events.clear();
        engine->midi(0x80,60,0);engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x80,60));
    }
    // Retired MIDI sinks retain their releases until the render thread sees
    // them, even if a second publication replaces the pending plan first.
    for(bool replaceType:{false,true}) {
        GraphSpec graph;
        graph.nodes={node(1,"soundnet.midi-input"),node(2,"soundnet.midi-output")};
        graph.nodes[1].parameters={{MidiOutChannel,4}};
        connect(graph,1,"notes",2,"notes");
        auto engine=std::make_unique<Engine>();engine->prepare(48000);assert(engine->setGraph(graph));
        std::vector<Sent> events;events.reserve(64);collect(*engine,events);
        float l[128]{},r[128]{};
        engine->midi(0x90,67,100);engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x93,67));events.clear();
        graph.edges.clear();
        if(replaceType)graph.nodes[1]=node(2,"soundnet.noise");
        else graph.nodes.pop_back();
        assert(engine->setGraph(graph));
        graph.nodes[0].x=40;assert(engine->setGraph(graph));
        engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x83,67));events.clear();
        engine->render(nullptr,nullptr,l,r,128);assert(events.empty());
    }
    // Node stop requests are independent mailboxes. Several requests before
    // the same audio boundary must stop every clip, including after republish.
    {
        GraphSpec graph;
        graph.nodes={node(1,"soundnet.start"),node(2,"soundnet.audio-clip"),
                     node(3,"soundnet.audio-clip"),node(4,"soundnet.audio-output"),
                     node(5,"soundnet.audio-output")};
        graph.nodes[0].parameters={{TriggerPulse,0}};
        for(size_t index=1;index<=2;++index) {
            auto sample=std::make_shared<AudioSample>();
            sample->rate=48000;sample->channels=2;sample->samples.resize(2048,0);
            for(size_t frame=0;frame<1024;++frame)sample->samples[frame*2+index-1]=.25f;
            graph.nodes[index].sample=std::move(sample);
            graph.nodes[index].parameters={{AudioClipGain,1},{AudioClipLoop,1},{AudioClipFade,0},{AudioClipXfade,0}};
        }
        graph.nodes[3].parameters=graph.nodes[4].parameters={{Master,1},{OutputWidth,1}};
        connect(graph,1,"trigger",2,"trigger");connect(graph,1,"trigger",3,"trigger");
        connect(graph,2,"audio",4,"audio");connect(graph,3,"audio",5,"audio");
        auto engine=std::make_unique<Engine>();engine->prepare(48000);assert(engine->setGraph(graph));
        float l[128]{},r[128]{};
        graph.nodes[0].parameters[TriggerPulse]=1;assert(engine->setGraph(graph));
        engine->render(nullptr,nullptr,l,r,128);assert(l[64]>.2f&&r[64]>.2f);
        engine->stopNode(2);engine->stopNode(3);
        graph.nodes[0].x=40;assert(engine->setGraph(graph));
        engine->render(nullptr,nullptr,l,r,128);
        // A zero-fade clip may finish its current first sample before release.
        for(int i=1;i<128;++i)assert(l[i]==0&&r[i]==0);
        graph.nodes[0].parameters[TriggerPulse]=2;assert(engine->setGraph(graph));
        engine->render(nullptr,nullptr,l,r,128);assert(l[64]>.2f&&r[64]>.2f);
    }
    // Musical tempo belongs to the clip, while explicit timecode seek chooses
    // the shared position in sample frames. The timecode node contains no BPM.
    {
        GraphSpec graph;
        GraphNode clip=node(2,"soundnet.midi-clip");
        clip.parameters={{ClockBPM,120},{ClipRun,1},{ClipLoop,0}};
        clip.clip={{{0,0x90,60,100},{.1,0x80,60,0},{2,0x90,72,100},{2.1,0x80,72,0}},4};
        graph.nodes={node(1,"soundnet.timecode"),clip,node(3,"soundnet.midi-output")};
        connect(graph,2,"notes",3,"notes");
        auto engine=std::make_unique<Engine>();engine->prepare(48000);assert(engine->setGraph(graph));
        std::vector<Sent> events;events.reserve(128);collect(*engine,events);
        float l[128],r[128];engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x90,60));events.clear();
        engine->seekTimecode(48000);engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x80,60)&&has(events,0x90,72));
        events.clear();engine->setProcessingEnabled(false);engine->seekTimecode(0);
        engine->render(nullptr,nullptr,l,r,128);assert(!has(events,0x90,60));
        events.clear();engine->setProcessingEnabled(true);engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x90,60));
    }
    // Observe the actual platform timeline rather than a private kernel field.
    // Global stop and a paused host freeze it; seek starts a new generation.
    {
        GraphSpec graph;graph.nodes={node(1,"soundnet.timecode")};
        auto engine=std::make_unique<Engine>();engine->prepare(48000);assert(engine->setGraph(graph));
        float l[257],r[257];engine->render(nullptr,nullptr,l,r,257);
        auto first=engine->timecodeSnapshot();
        assert(first.framePosition==257&&first.sampleRate==48000&&first.playing);
        engine->setProcessingEnabled(false);engine->render(nullptr,nullptr,l,r,128);
        auto stopped=engine->timecodeSnapshot();
        assert(stopped.framePosition==257&&!stopped.playing&&stopped.generation>first.generation);
        engine->setProcessingEnabled(true);engine->render(nullptr,nullptr,l,r,64);
        auto resumed=engine->timecodeSnapshot();
        assert(resumed.framePosition==321&&resumed.playing);
        engine->seekTimecode(96000);engine->render(nullptr,nullptr,l,r,128);
        auto seeked=engine->timecodeSnapshot();
        assert(seeked.framePosition==96128&&seeked.generation>resumed.generation);
        engine->setHostTransport(false,0);engine->render(nullptr,nullptr,l,r,128);
        auto paused=engine->timecodeSnapshot();
        assert(paused.framePosition==96128&&!paused.playing);
        engine->setHostTempo(240);engine->setHostTransport(true,0);engine->render(nullptr,nullptr,l,r,128);
        auto hosted=engine->timecodeSnapshot();
        assert(hosted.framePosition==96256&&hosted.playing); // no global BPM scaling
        // Host sample positions that match the expected next block are normal
        // progression, not repeated seeks that would reset every node each block.
        engine->setHostTimecode(true,96256);engine->render(nullptr,nullptr,l,r,128);
        auto continuous=engine->timecodeSnapshot();
        assert(continuous.framePosition==96384&&continuous.generation==hosted.generation);
        engine->setHostTimecode(true,96384);engine->render(nullptr,nullptr,l,r,128);
        continuous=engine->timecodeSnapshot();
        assert(continuous.framePosition==96512&&continuous.generation==hosted.generation);
        engine->setHostTimecode(false,96512);engine->render(nullptr,nullptr,l,r,128);
        engine->setHostTimecode(false,96512);engine->render(nullptr,nullptr,l,r,128);
        auto hostPaused=engine->timecodeSnapshot();
        assert(hostPaused.framePosition==96512&&!hostPaused.playing&&hostPaused.generation==hosted.generation);
        engine->setHostTimecode(true,1200);engine->render(nullptr,nullptr,l,r,64);
        auto hostSeek=engine->timecodeSnapshot();
        assert(hostSeek.framePosition==1264&&hostSeek.playing&&hostSeek.generation>hosted.generation);
    }
    puts("PASS: real Engine SDK audio, MIDI ports/releases/capacity, independent node stops and timecode continuity/seek");
}
