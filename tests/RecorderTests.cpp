#include "../app/AudioBridge.hpp"
#include "../src/WavFile.hpp"
#include <cstdio>
#include <cassert>
#include <cstdio>
#include <vector>

int main() {
    TRIRecorder recorder;
    recorder.start(48000, 0.01);
    assert(recorder.active.load());
    float l[4] = {0.1f, 0.2f, -0.3f, 0.4f};
    float r[4] = {-0.1f, 0.25f, 0.35f, -0.45f};
    recorder.capture(l, r, 4);
    assert(recorder.frames() == 4);
    assert(recorder.left[2] == -0.3f && recorder.right[3] == -0.45f);
    recorder.stop();
    assert(!recorder.active.load());
    recorder.capture(l, r, 4);
    assert(recorder.frames() == 4);

    recorder.start(8000, 0.001); // eight frames
    float block[16] = {};
    recorder.capture(block, block, 16);
    assert(recorder.frames() == 8);
    assert(recorder.overflow.load());
    recorder.stop();

    TRIRealtime realtime;
    realtime.dsp.prepare(48000);
    realtime.recording.start(48000, 1.0);
    float outL[128] = {}, outR[128] = {};
    realtime.render(outL, outR, 128, false, 0);
    assert(realtime.recording.frames() == 128);
    realtime.recording.stop();

    realtime.inputRecording.start(48000, 1.0);
    realtime.render(outL, outR, 128, false, 0);
    assert(realtime.inputRecording.frames() == 128);
    realtime.inputRecording.stop();

    TRIRecorder nulls;
    nulls.start(48000, 1.0);
    nulls.capture(nullptr, nullptr, 4);
    assert(nulls.frames() == 4 && nulls.left[0] == 0.f && nulls.right[3] == 0.f);

    std::vector<float> left(realtime.recording.left.begin(), realtime.recording.left.begin() + 128);
    std::vector<float> right(realtime.recording.right.begin(), realtime.recording.right.begin() + 128);
    std::string why;
    const std::string path = "/tmp/soundnet-recorder-test.wav";
    assert(tri::wav::write(path, left, right, 48000, why));
    FILE *file = std::fopen(path.c_str(), "rb");
    assert(file && std::fseek(file, 0, SEEK_END) == 0 && std::ftell(file) > 44);
    std::fclose(file);
    std::remove(path.c_str());

    // Standalone transport drops scheduled future MIDI as well as live input.
    // A Stop/Play pair before the next callback still drains the old queue.
    tri::GraphSpec graph;graph.nodes={{1,tri::MidiNode},{2,tri::Osc1Node},{3,tri::HardwareOutputNode}};
    graph.edges={{1,2,0},{2,3,0}};assert(realtime.dsp.setGraph(graph));
    realtime.framesPerTick=1;
    realtime.enqueue(0x90,60,127,10000);
    realtime.render(outL,outR,128,false,100);
    assert(realtime.count==1);
    const uint64_t previousGeneration=realtime.dsp.processingGeneration();
    realtime.setProcessingEnabled(false);realtime.enqueue(0x90,72,127);
    realtime.setProcessingEnabled(true);
    realtime.render(outL,outR,128,false,10000);
    assert(realtime.count==0);
    for(int i=0;i<128;++i)assert(outL[i]==0 && outR[i]==0);
    // Simulate a producer that reserved a FIFO slot before Stop but published
    // it only after the queue drain; its old generation must still be ignored.
    assert(realtime.midi.push({0,0,0x90,72,127,false,previousGeneration}));
    realtime.render(outL,outR,128,false,0);
    for(int i=0;i<128;++i)assert(outL[i]==0 && outR[i]==0);
    // Fresh input after a rapid Stop -> Play belongs to the new generation.
    realtime.setProcessingEnabled(false);realtime.setProcessingEnabled(true);
    realtime.enqueue(0x90,67,127);
    bool audible=false;
    for(int b=0;b<8;++b){realtime.render(outL,outR,128,false,0);for(float v:outL)audible|=std::abs(v)>.001f;}
    assert(audible);
    realtime.setProcessingEnabled(false);
    realtime.render(outL,outR,128,false,0);
    for(int i=0;i<128;++i)assert(outL[i]==0 && outR[i]==0);

    // Legacy patches obey the same gate without rewriting their parameters.
    realtime.dsp.clearGraph();realtime.setProcessingEnabled(true);
    realtime.dsp.set(tri::RouteOutputIn,tri::Osc1Node);
    realtime.dsp.set(tri::RouteOsc1In,tri::MidiNode);
    realtime.render(outL,outR,128,false,0);
    realtime.enqueue(0x90,69,127);
    audible=false;
    for(int b=0;b<8;++b){realtime.render(outL,outR,128,false,0);for(float v:outL)audible|=std::abs(v)>.001f;}
    assert(audible);
    realtime.setProcessingEnabled(false);realtime.setProcessingEnabled(true);
    realtime.render(outL,outR,128,false,0);
    for(int i=0;i<128;++i)assert(outL[i]==0 && outR[i]==0);

    realtime.setProcessingEnabled(false);realtime.setProcessingEnabled(true);
    realtime.enqueue(0x90,72,127);
    audible=false;
    for(int b=0;b<8;++b){realtime.render(outL,outR,128,false,0);for(float v:outL)audible|=std::abs(v)>.001f;}
    assert(audible);

    puts("PASS: realtime input/output recorders copy, stop and report overflow.");
}
