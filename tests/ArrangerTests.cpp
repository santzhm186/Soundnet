#include "../src/Engine.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>

using namespace tri;

// Observe actual downstream MIDI, including whether a voice was released twice.
struct Output {
    std::map<int, int> sounding;
    std::vector<int> frames;
    unsigned attacks = 0;
    static void receive(void* context, int frame, uint8_t status, uint8_t pitch, uint8_t velocity) {
        auto& out = *static_cast<Output*>(context);
        int key = (status & 15) * 128 + pitch;
        if ((status & 0xf0) == 0x90 && velocity > 0) {
            assert(out.sounding.emplace(key, velocity).second);
            ++out.attacks;
        } else if ((status & 0xf0) == 0x80 || (status & 0xf0) == 0x90) {
            assert(out.sounding.erase(key) == 1);
        } else return;
        out.frames.push_back(frame);
    }
};

struct Session {
    Output output;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    GraphSpec graph;

    Session(std::initializer_list<int> ports) {
        graph.nodes = {{1, MidiNode}, {2, ArrangerNode}};
        graph.edges = {{1, 2, 0}};
        for (int port : ports) {
            InstanceID sink = 10 + port;
            graph.nodes.push_back({sink, MidiOutNode});
            graph.edges.push_back({2, sink, 0, port});
        }
        engine->prepare(48000);
        engine->setMidiOutput(&output, Output::receive);
        publish();
        tick();
    }
    void publish() { assert(engine->setGraph(graph)); }
    void tick() {
        std::array<float, 128> left{}, right{};
        engine->render(nullptr, nullptr, left.data(), right.data(), int(left.size()));
    }
    void key(int pitch, int velocity, int channel = 0) {
        engine->midi(uint8_t((velocity ? 0x90 : 0x80) | channel), uint8_t(pitch), uint8_t(velocity));
    }
    void expect(std::map<int,int> notes) const { assert(output.sounding == notes); }
};

int main() {
    {
        Session s{0, 1, 2};
        s.key(60, 85); s.key(64, 73); s.key(67, 109); s.tick();
        s.expect({{48,85}, {64,73}, {79,109}});
        assert(s.output.attacks == 3);  // simultaneous chord, no intermediate melody notes
        s.key(67, 0); s.tick();
        s.expect({{48,85}, {60,85}, {76,73}}); // two-note chord uses the lower median
        s.key(64, 0); s.key(60, 0); s.tick();
        s.expect({});
    }
    {
        Session s{1};
        auto& p = s.graph.nodes[1].parameters;
        p = {{ArrMidMin,60}, {ArrMidMax,71}, {ArrMidTexture,int(ArrangerTexture::Chord)}};
        s.publish();
        s.key(48, 90, 2); s.key(64, 70, 2); s.tick();
        s.expect({{256+60,90}, {256+64,70}});
        s.key(72, 90, 2); s.tick(); // another C folds onto the same channel and pitch
        assert(s.output.sounding.size() == 2);
        s.key(48, 0, 2); s.tick();
        assert(s.output.sounding.count(256+60));
        s.key(60, 50, 3); s.tick(); // same pitch on a different channel stays independent
        s.expect({{256+60,90}, {256+64,70}, {384+60,50}});
        p[ArrMidMax] = 60; s.publish(); s.tick();
        s.expect({{256+60,90}, {384+60,50}});
        s.key(72, 0, 2); s.key(60, 0, 3); s.tick();
        s.expect({}); // E remains held, but cannot fit into this single-note range
        p[ArrMidMin] = 0; s.publish(); s.tick();
        s.expect({{256+52,70}});
        s.key(64, 0, 2); s.tick(); s.expect({});
    }
    {
        Session s{0};
        s.key(60, 95); s.tick(); s.expect({{72,95}});
        s.graph.edges.back().sourcePort = 2; s.publish(); s.tick();
        s.expect({{48,95}}); // rewire a held note from the high to the low outlet
        s.graph.edges.erase(s.graph.edges.begin()); s.publish(); s.tick(); s.expect({});
        s.graph.edges.push_back({1,2,0}); s.publish(); s.tick(); s.expect({{48,95}});
        s.engine->panic(); s.tick(); s.expect({});
    }
    {
        Session s{2};
        auto& clip = s.graph.nodes.front();
        clip.type = ClipNode;
        clip.nodeVersion = nodeSpec(ClipNode)->nodeVersion;
        // At 120 BPM and 48 kHz, the events land 24 and 72 samples into the block.
        clip.clip = {{{.001,0x91,60,83},{.003,0x81,60,0}},1};
        clip.parameters = {{ClipRun,1},{ClipLoop,0},{ClockBPM,120}};
        s.graph.nodes.push_back({20,nodeTypeID("soundnet.timecode")});
        s.publish();
        // The session has already advanced one block. A newly inserted clip
        // uses the shared timeline rather than implicitly starting its own.
        s.engine->seekTimecode(0);
        s.tick();
        assert(s.output.frames.size() == 2);
        assert(std::abs(s.output.frames[0]-24)<=1 && std::abs(s.output.frames[1]-72)<=1);
        s.expect({});
    }
    puts("PASS: three MIDI outlets, voicing, ranges, channel/velocity, live edits, panic and clip timing.");
}
