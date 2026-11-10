#include "NodeTestLookup.hpp"
#include "../src/Engine.hpp"
#include "../src/StandardMidiFile.hpp"
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
    // MIDI Clip channel events follow its local tempo and reach the real sink.
    // A controller-only clip can be stored and played without fake notes.
    {
        MidiClip source{{{0,0xc2,10,0},{0,0xd2,75,0},{0,0xb2,64,127},
                         {0,0xe2,0,96},{.25,0xb2,11,91},{.5,0xb2,1,32}},.5};
        const auto bytes=smf::encodeClip(source);
        GraphNode clip=node(1,"soundnet.midi-clip");
        clip.clip=smf::parse(bytes.data(),bytes.size(),true);
        assert(clip.clip.events.size()==6 && clip.clip.events[1].status==0xd2);
        clip.parameters={{ntest::param("midi-clip","clip.run"),1},{ntest::param("midi-clip","clip.loop"),0},{ntest::param("midi-clip","clock.bpm"),120}};
        GraphSpec graph;graph.nodes={clip,node(2,"soundnet.midi-output")};
        connect(graph,1,"notes",2,"notes");
        auto engine=std::make_unique<Engine>();engine->prepare(48000);assert(engine->setGraph(graph));
        std::vector<Sent> events;events.reserve(20);collect(*engine,events);
        float left[128]{},right[128]{};
        engine->render(nullptr,nullptr,left,right,128);
        assert(has(events,0xc2,10)&&has(events,0xd2,75)&&has(events,0xb2,64)&&has(events,0xe2,0));
        assert(!has(events,0xb2,11));
        events.clear();
        for(int i=128;i<12032;i+=128)engine->render(nullptr,nullptr,left,right,128);
        assert(has(events,0xb2,11)&&has(events,0xb2,1));
        assert(std::any_of(events.begin(),events.end(),[](const Sent& e){
            return e.status==0xb2&&e.note==64&&e.velocity==0;
        }));
        assert(std::any_of(events.begin(),events.end(),[](const Sent& e){
            return e.status==0xe2&&e.note==0&&e.velocity==64;
        }));
        engine->seekTimecode(0);engine->render(nullptr,nullptr,left,right,128);
        events.clear();
        graph.edges.clear();assert(engine->setGraph(graph));
        engine->render(nullptr,nullptr,left,right,128);
        assert(events.size()==3 && has(events,0xb2,64) && has(events,0xe2,0) && has(events,0xd2,0));
        graph.nodes[0].clip={{{0,0x92,60,100},{.001,0x82,60,44}},.25};
        connect(graph,1,"notes",2,"notes");
        assert(engine->setGraph(graph));
        engine->seekTimecode(0);events.clear();
        engine->render(nullptr,nullptr,left,right,128);
        assert(events.size()==2 && events[1].status==0x82 && events[1].velocity==44);
    }
    // The same graph receives device or host buffers. Gain is per instance,
    // and mono input and offline rendering both have defined fallback audio.
    {
        GraphSpec graph;
        graph.nodes={node(1,"soundnet.audio-input"),node(2,"soundnet.audio-output")};
        graph.nodes[0].parameters={{ntest::param("audio-input","input.gain"),.5f}};
        graph.nodes[1].parameters={{ntest::param("audio-output","output.master"),1},{ntest::param("audio-output","output.width"),1}};
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
        graph.nodes[0].parameters[ntest::param("audio-input","input.gain")]=0;
        assert(engine->setGraph(graph));
        engine->render(inL,inR,left,right,3);
        assert(left[2]==0 && right[2]==0);
    }
    // Existing generators and outputs communicate with a separately registered
    // kernel using named audio ports. Tail blocks and stereo survive the bridge.
    {
        GraphSpec reference;
        reference.nodes={node(1,"soundnet.noise"),node(3,"soundnet.audio-output")};
        reference.nodes[0].parameters={{ntest::param("noise","noise.level"),.3f}};reference.nodes[1].parameters={{ntest::param("audio-output","output.master"),1},{ntest::param("audio-output","output.width"),1}};
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
        graph.nodes[2].parameters={{ntest::param("midi-output","midiOut.channel"),2}};
        graph.nodes[3].parameters={{ntest::param("midi-output","midiOut.channel"),3}};
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
        graph.nodes[1].parameters={{ntest::param("midi-output","midiOut.channel"),2}};
        connect(graph,1,"notes",2,"notes");
        auto engine=std::make_unique<Engine>();engine->prepare(48000);assert(engine->setGraph(graph));
        std::vector<Sent> events;events.reserve(64);collect(*engine,events);
        float l[128]{},r[128]{};
        engine->midi(0x90,60,100);engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x91,60));events.clear();
        graph.nodes[1].parameters[ntest::param("midi-output","midiOut.channel")]=3;assert(engine->setGraph(graph));
        engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x81,60)&&has(events,0x92,60));events.clear();
        // Several unconsumed edits must use the last rendered channel, not an
        // intermediate configuration that has never sent any note-on.
        graph.nodes[1].parameters[ntest::param("midi-output","midiOut.channel")]=4;assert(engine->setGraph(graph));
        graph.nodes[1].parameters[ntest::param("midi-output","midiOut.channel")]=0;assert(engine->setGraph(graph));
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
        graph.nodes[1].parameters={{ntest::param("midi-output","midiOut.channel"),4}};
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
        graph.nodes[0].parameters={{ntest::param("start","trigger.pulse"),0}};
        for(size_t index=1;index<=2;++index) {
            auto sample=std::make_shared<AudioSample>();
            sample->rate=48000;sample->channels=2;sample->samples.resize(2048,0);
            for(size_t frame=0;frame<1024;++frame)sample->samples[frame*2+index-1]=.25f;
            graph.nodes[index].sample=std::move(sample);
            graph.nodes[index].parameters={{ntest::param("audio-clip","audioClip.gain"),1},{ntest::param("audio-clip","audioClip.loop"),1},{ntest::param("audio-clip","audioClip.fade"),0},{ntest::param("audio-clip","audioClip.loopXfade"),0}};
        }
        graph.nodes[3].parameters=graph.nodes[4].parameters={{ntest::param("audio-output","output.master"),1},{ntest::param("audio-output","output.width"),1}};
        connect(graph,1,"trigger",2,"trigger");connect(graph,1,"trigger",3,"trigger");
        connect(graph,2,"audio",4,"audio");connect(graph,3,"audio",5,"audio");
        auto engine=std::make_unique<Engine>();engine->prepare(48000);assert(engine->setGraph(graph));
        float l[128]{},r[128]{};
        graph.nodes[0].parameters[ntest::param("start","trigger.pulse")]=1;assert(engine->setGraph(graph));
        engine->render(nullptr,nullptr,l,r,128);assert(l[64]>.2f&&r[64]>.2f);
        engine->stopNode(2);engine->stopNode(3);
        graph.nodes[0].x=40;assert(engine->setGraph(graph));
        engine->render(nullptr,nullptr,l,r,128);
        // A zero-fade clip may finish its current first sample before release.
        for(int i=1;i<128;++i)assert(l[i]==0&&r[i]==0);
        graph.nodes[0].parameters[ntest::param("start","trigger.pulse")]=2;assert(engine->setGraph(graph));
        engine->render(nullptr,nullptr,l,r,128);assert(l[64]>.2f&&r[64]>.2f);
    }
    // A queued cycle belongs to its original runtime and wire. Before the
    // lifecycle fix, each cancelled case below emitted an extra Note On at
    // frame 24 of the next block. A canvas-only edit must retain that event.
    for(int change=0;change<4;++change) {
        GraphSpec graph;
        graph.nodes={node(1,"soundnet.midi-clip"),node(2,"soundnet.midi-output")};
        auto& clip=graph.nodes[0];
        clip.parameters={{ntest::param(clip,"clip.run"),1},{ntest::param(clip,"clip.loop"),0},
                         {ntest::param(clip,"clock.bpm"),120}};
        clip.clip={{{0,0x90,60,100},{.0009,0x80,60,0}},.001};
        connect(graph,1,"cycle",1,"trigger");connect(graph,1,"notes",2,"notes");
        auto engine=std::make_unique<Engine>();engine->prepare(48000);assert(engine->setGraph(graph));
        std::vector<Sent> events;events.reserve(32);collect(*engine,events);
        float l[128]{},r[128]{};engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x90,60)&&has(events,0x80,60));events.clear();
        if(change==0)engine->stopNode(1);
        else {
            if(change==1)clip.parameters[ntest::param(clip,"clip.run")]=0;
            if(change==2)graph.edges.erase(graph.edges.begin());
            if(change==3)clip.x=48;
            assert(engine->setGraph(graph));
        }
        engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x90,60)==(change==3));
        if(change==3)assert(std::any_of(events.begin(),events.end(),[](const Sent& e){
            return e.status==0x90&&e.note==60&&e.frame==24;
        }));
    }
    // Multiple trigger sources are merged in timestamp order. The later
    // branch happens to precede the earlier branch in the topology. Appending
    // without sorting produced [start@96, stop@24] and suppressed all notes.
    {
        GraphSpec graph;
        graph.nodes={node(1,"soundnet.start"),node(2,"soundnet.wait"),node(3,"soundnet.wait"),
                     node(4,"soundnet.stop"),node(5,"soundnet.midi-clip"),node(6,"soundnet.midi-output")};
        graph.nodes[1].parameters[ntest::param(graph.nodes[1],"wait.seconds")]=.002f;
        graph.nodes[2].parameters[ntest::param(graph.nodes[2],"wait.seconds")]=.0005f;
        auto& clip=graph.nodes[4];
        clip.parameters={{ntest::param(clip,"clip.run"),0},{ntest::param(clip,"clip.loop"),0},
                         {ntest::param(clip,"clock.bpm"),120}};
        clip.clip={{{0,0x90,60,100},{.5,0x80,60,0}},1};
        connect(graph,1,"trigger",2,"trigger");connect(graph,1,"trigger",3,"trigger");
        connect(graph,2,"trigger",5,"trigger");connect(graph,3,"trigger",4,"trigger");
        connect(graph,4,"trigger",5,"trigger");connect(graph,5,"notes",6,"notes");
        auto engine=std::make_unique<Engine>();engine->prepare(48000);assert(engine->setGraph(graph));
        std::vector<Sent> events;events.reserve(16);collect(*engine,events);
        graph.nodes[0].parameters[ntest::param(graph.nodes[0],"trigger.pulse")]=1;assert(engine->setGraph(graph));
        float l[128]{},r[128]{};engine->render(nullptr,nullptr,l,r,128);
        assert(events.size()==1&&events[0].status==0x90&&events[0].note==60&&events[0].frame==96);
    }
    // Channel-mode release messages clear the platform's held-note snapshot.
    // Otherwise replacing a downstream kernel chases an already released note
    // (the original failure emitted Note On 61 after CC123 and a transpose edit).
    for(int controller:{120,123,124,125,126,127}) {
        GraphSpec graph;
        graph.nodes={node(1,"soundnet.midi-input"),node(2,"soundnet.transpose"),node(3,"soundnet.midi-output")};
        connect(graph,1,"notes",2,"notes");connect(graph,2,"notes",3,"notes");
        auto engine=std::make_unique<Engine>();engine->prepare(48000);assert(engine->setGraph(graph));
        std::vector<Sent> events;events.reserve(32);collect(*engine,events);float l[128]{},r[128]{};
        engine->midi(0x90,60,100);engine->render(nullptr,nullptr,l,r,128);assert(has(events,0x90,60));
        engine->midi(0xb0,uint8_t(controller),0);engine->render(nullptr,nullptr,l,r,128);events.clear();
        graph.nodes[1].parameters[ntest::param(graph.nodes[1],"transpose.semitones")]=1;
        assert(engine->setGraph(graph));engine->render(nullptr,nullptr,l,r,128);
        assert(!has(events,0x90,60)&&!has(events,0x90,61));
    }
    // Musical tempo belongs to the clip, while explicit timecode seek chooses
    // the shared position in sample frames. The timecode node contains no BPM.
    {
        GraphSpec graph;
        GraphNode clip=node(2,"soundnet.midi-clip");
        clip.parameters={{ntest::param("midi-clip","clock.bpm"),120},{ntest::param("midi-clip","clip.run"),1},{ntest::param("midi-clip","clip.loop"),0}};
        clip.clip={{{0,0x90,60,100},{.1,0x80,60,0},{2,0x90,72,100},{2.1,0x80,72,0}},4};
        graph.nodes={node(1,"soundnet.timecode"),clip,node(3,"soundnet.midi-output")};
        connect(graph,2,"notes",3,"notes");
        auto engine=std::make_unique<Engine>();engine->prepare(48000);assert(engine->setGraph(graph));
        std::vector<Sent> events;events.reserve(128);collect(*engine,events);
        float l[128],r[128];engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x90,60));events.clear();
        engine->seekTimecode(48000);engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x80,60)&&has(events,0x90,72));
        events.clear();engine->setProcessingEnabled(false);
        // Publishing even a layout-only edit while paused must not overwrite
        // the subsequent Seek reset with a second Stop reset.
        graph.nodes[1].x=48;assert(engine->setGraph(graph));engine->seekTimecode(0);
        engine->render(nullptr,nullptr,l,r,128);assert(!has(events,0x90,60));
        events.clear();engine->setProcessingEnabled(true);engine->render(nullptr,nullptr,l,r,128);
        assert(has(events,0x90,60));
    }
    // Host pause releases MIDI Clip notes/controllers and freezes its cursor;
    // realtime MIDI audition remains available through its independent input.
    {
        GraphSpec graph;
        auto clip=node(1,"soundnet.midi-clip");
        clip.parameters={{ntest::param(clip,"clock.bpm"),120},{ntest::param(clip,"clip.run"),1},
                         {ntest::param(clip,"clip.loop"),0}};
        clip.clip={{{0,0x92,60,100},{0,0xb2,64,127},{0,0xe2,0,96},
                    {.01,0x82,60,23},{.012,0xb2,64,0}},1};
        graph.nodes={clip,node(2,"soundnet.midi-output"),node(3,"soundnet.midi-input"),node(4,"soundnet.midi-output")};
        connect(graph,1,"notes",2,"notes");connect(graph,3,"notes",4,"notes");
        auto engine=std::make_unique<Engine>();engine->prepare(48000);assert(engine->setGraph(graph));
        std::vector<Sent> events;events.reserve(128);collect(*engine,events);float l[128]{},r[128]{};
        auto sent=[&](uint8_t status,uint8_t note,uint8_t value,int frame) {
            return std::any_of(events.begin(),events.end(),[&](const Sent& e){
                return e.status==status&&e.note==note&&e.velocity==value&&e.frame==frame;
            });
        };
        engine->render(nullptr,nullptr,l,r,64);assert(sent(0x92,60,100,0));events.clear();
        engine->setHostTimecode(false,64);engine->render(nullptr,nullptr,l,r,128);
        assert(sent(0x82,60,0,0)&&sent(0xb2,64,0,0)&&sent(0xe2,0,64,0));events.clear();
        engine->midi(0x93,72,88);engine->render(nullptr,nullptr,l,r,128);
        assert(events.size()==1&&sent(0x93,72,88,0));events.clear();
        engine->midi(0x83,72,19);engine->render(nullptr,nullptr,l,r,128);
        assert(events.size()==1&&sent(0x83,72,19,0));events.clear();
        assert(engine->timecodeSnapshot().framePosition==64);
        engine->setHostTimecode(true,64);engine->render(nullptr,nullptr,l,r,128);
        assert(sent(0x92,60,100,0)&&sent(0xb2,64,127,0)&&sent(0xe2,0,96,0)&&!has(events,0x82,60));events.clear();
        engine->render(nullptr,nullptr,l,r,64);assert(events.size()==1&&sent(0x82,60,23,48));events.clear();
        engine->setHostTimecode(false,256);engine->render(nullptr,nullptr,l,r,128);events.clear();
        engine->setHostTimecode(false,120);engine->render(nullptr,nullptr,l,r,128);assert(events.empty());
        engine->render(nullptr,nullptr,l,r,128);assert(events.empty());
        assert(engine->timecodeSnapshot().framePosition==120);
        engine->setHostTimecode(true,120);engine->render(nullptr,nullptr,l,r,128);
        assert(sent(0x92,60,100,0)&&sent(0x82,60,23,120));
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
