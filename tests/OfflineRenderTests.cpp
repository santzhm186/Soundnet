#include "NodeTestLookup.hpp"
#include "../src/OfflineRender.hpp"
#include "../src/WavFile.hpp"
#include "../src/AudioFile.hpp"
#include "../src/MidiFileWriter.hpp"
#include "../src/StandardMidiFile.hpp"
#include <cassert>
#include <fstream>
#include <cstdio>
#include <string>
#include <filesystem>

using namespace tri;

static void edge(GraphSpec& graph,uint64_t source,uint64_t target,int port=0,int sourcePort=0) {
    graph.edges.push_back({source,target,port,sourcePort});
}

int main() {
    // Exports go to build/tests-offline; a clean tree has no such directory yet.
    std::filesystem::create_directories("build/tests-offline");
    GraphSpec graph;
    GraphNode start;start.id=1;start.type=ntest::type("start");start.x=0;start.y=0;graph.nodes.push_back(start);
    GraphNode clip;clip.id=2;clip.type=ntest::type("midi-clip");clip.nodeVersion=nodeSpec(ntest::type("midi-clip"))->nodeVersion;clip.x=120;clip.y=0;
    clip.clip.lengthBeats=2;
    clip.clip.events={{0,0x90,60,100},{1,0x80,60,0}};
    graph.nodes.push_back(clip);
    GraphNode osc;osc.id=3;osc.type=ntest::type("oscillator");osc.x=240;osc.y=0;osc.parameters[ntest::param("oscillator","osc1.level")]=.6f;graph.nodes.push_back(osc);
    GraphNode out;out.id=4;out.type=ntest::type("audio-output");out.nodeVersion=nodeSpec(ntest::type("audio-output"))->nodeVersion;out.x=360;out.y=0;out.parameters[ntest::param("audio-output","output.master")]=1;graph.nodes.push_back(out);
    edge(graph,1,2);edge(graph,2,3);edge(graph,3,4);
    std::vector<size_t> order;assert(graphOrder(graph,order));

    offline::Result result;std::string why;
    assert(offline::render(graph,1,48000,result,why));
    assert(result.left.size()==48000 && result.right.size()==48000);
    double peak=0;for(float value:result.left)peak=std::max(peak,double(std::fabs(value)));
    assert(peak>.01);
    assert(result.peak>.01);
    puts("start-triggered graph renders offline");

    GraphSpec midiGraph;
    GraphNode midiStart;midiStart.id=1;midiStart.type=ntest::type("start");midiGraph.nodes.push_back(midiStart);
    GraphNode midiClip;midiClip.id=2;midiClip.type=ntest::type("midi-clip");midiClip.nodeVersion=nodeSpec(ntest::type("midi-clip"))->nodeVersion;midiClip.clip.lengthBeats=1;
    midiClip.clip.events={{0,0x90,67,110},{.5,0x80,67,0}};
    midiClip.parameters[ntest::param("midi-clip","clock.bpm")]=90; // tempo belongs to this clip
    midiGraph.nodes.push_back(midiClip);
    GraphNode midiOut;midiOut.id=3;midiOut.type=ntest::type("midi-output");midiGraph.nodes.push_back(midiOut);
    edge(midiGraph,1,2);edge(midiGraph,2,3);
    GraphNode midiClock;midiClock.id=4;midiClock.type=nodeTypeID("soundnet.timecode");
    midiGraph.nodes.push_back(midiClock);
    std::vector<size_t> midiOrder;assert(graphOrder(midiGraph,midiOrder));
    offline::Result midiResult;
    assert(offline::render(midiGraph,2,48000,midiResult,why));
    assert(!midiResult.midi.empty());
    bool on=false,off=false;for(const auto& e:midiResult.midi){if((e.status&0xf0)==0x90)on=true;if((e.status&0xf0)==0x80)off=true;}
    assert(on&&off);
    std::string midiPath="build/tests-offline/export.mid";
    std::vector<smf::TimedEvent> timed;
    for(const auto& e:midiResult.midi)timed.push_back({e.seconds,e.status,e.a,e.b});
    // SMF uses a neutral conversion tempo to preserve absolute event seconds;
    // it does not impose one project tempo on independently timed nodes.
    assert(midiResult.bpm==120);
    double firstRelease=-1;
    for(const auto& event:midiResult.midi)if((event.status&0xf0)==0x80){firstRelease=event.seconds;break;}
    assert(std::abs(firstRelease-1.0/3.0)<2.0/48000.0);
    assert(smf::writeMulti(midiPath,timed,2,midiResult.bpm,why));
    std::ifstream in(midiPath,std::ios::binary);std::string midiBytes((std::istreambuf_iterator<char>(in)),{});
    assert(midiBytes.rfind("MThd",0)==0);
    assert(midiBytes.size()>14 && midiBytes[9]==1 && midiBytes[11]>=2);
    MidiClip roundTrip;roundTrip=smf::parse(reinterpret_cast<const uint8_t*>(midiBytes.data()),midiBytes.size());
    assert(!roundTrip.events.empty());
    puts("MIDI output is captured and written as a multi-track SMF");

    std::string path="build/tests-offline/export.wav";
    assert(wav::write(path,result.left,result.right,result.rate,why));
    std::string bytes;
    {
        FILE *file=std::fopen(path.c_str(),"rb");assert(file);
        std::fseek(file,0,SEEK_END);long size=std::ftell(file);std::fseek(file,0,SEEK_SET);
        assert(size>44+48000);
        bytes.resize(size_t(size));assert(std::fread(bytes.data(),1,bytes.size(),file)==bytes.size());
        std::fclose(file);
    }
    assert(bytes.rfind("RIFF",0)==0 && bytes.compare(8,4,"WAVE")==0);
    AudioSample decoded;
    assert(audiofile::parse(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),decoded,why));
    assert(decoded.frames()==48000 && decoded.channels==2 && std::abs(decoded.rate-48000)<.1);
    puts("offline render writes a stereo WAV");

    std::string path24="build/tests-offline/export24.wav";
    assert(wav::write(path24,result.left,result.right,result.rate,why,24));
    std::string bytes24;
    {
        FILE *file=std::fopen(path24.c_str(),"rb");assert(file);
        std::fseek(file,0,SEEK_END);long size=std::ftell(file);std::fseek(file,0,SEEK_SET);
        bytes24.resize(size_t(size));assert(std::fread(bytes24.data(),1,bytes24.size(),file)==bytes24.size());
        std::fclose(file);
    }
    assert(bytes24.rfind("RIFF",0)==0);
    assert(uint8_t(bytes24[34])==24 && uint8_t(bytes24[35])==0);
    AudioSample decoded24;
    assert(audiofile::parse(reinterpret_cast<const uint8_t*>(bytes24.data()),bytes24.size(),decoded24,why));
    assert(decoded24.frames()==48000 && decoded24.channels==2);
    puts("24-bit export round-trips through the WAV reader");
    std::string path32="build/tests-offline/export32f.wav";
    assert(wav::write(path32,result.left,result.right,result.rate,why,32));
    std::string bytes32;
    {
        FILE *file=std::fopen(path32.c_str(),"rb");assert(file);
        std::fseek(file,0,SEEK_END);long size=std::ftell(file);std::fseek(file,0,SEEK_SET);
        bytes32.resize(size_t(size));assert(std::fread(bytes32.data(),1,bytes32.size(),file)==bytes32.size());
        std::fclose(file);
    }
    assert(bytes32.rfind("RIFF",0)==0);
    assert(uint8_t(bytes32[20])==3 && uint8_t(bytes32[21])==0);
    assert(uint8_t(bytes32[34])==32 && uint8_t(bytes32[35])==0);
    AudioSample decoded32;
    assert(audiofile::parse(reinterpret_cast<const uint8_t*>(bytes32.data()),bytes32.size(),decoded32,why));
    assert(decoded32.frames()==48000 && decoded32.channels==2);
    puts("32-bit float export round-trips through the WAV reader");

    offline::Result rateResult;
    assert(offline::render(graph,1,44100,rateResult,why));
    assert(std::abs(rateResult.rate-44100)<.1 && rateResult.left.size()==44100);
    puts("44.1 kHz offline rendering is supported");

    GraphSpec stemGraph;
    GraphNode noise;noise.id=1;noise.type=ntest::type("noise");noise.parameters[ntest::param("noise","noise.level")]=.5f;stemGraph.nodes.push_back(noise);
    GraphNode outA;outA.id=2;outA.type=ntest::type("audio-output");outA.nodeVersion=nodeSpec(ntest::type("audio-output"))->nodeVersion;outA.parameters[ntest::param("audio-output","output.master")]=1;stemGraph.nodes.push_back(outA);
    GraphNode noiseB;noiseB.id=3;noiseB.type=ntest::type("noise");noiseB.parameters[ntest::param("noise","noise.level")]=.3f;stemGraph.nodes.push_back(noiseB);
    GraphNode outB;outB.id=4;outB.type=ntest::type("audio-output");outB.nodeVersion=nodeSpec(ntest::type("audio-output"))->nodeVersion;outB.parameters[ntest::param("audio-output","output.master")]=1;stemGraph.nodes.push_back(outB);
    edge(stemGraph,1,2);edge(stemGraph,3,4);
    std::vector<offline::Stem> stems;
    assert(offline::renderStems(stemGraph,.25,48000,stems,why));
    assert(stems.size()==2);
    for(const offline::Stem& stem:stems){assert(stem.result.peak>.01);assert(stem.name.find('#')!=std::string::npos);}
    GraphSpec noOutput;
    GraphNode lonely;lonely.id=1;lonely.type=ntest::type("oscillator");noOutput.nodes.push_back(lonely);
    stems.clear();
    assert(!offline::renderStems(noOutput,.1,48000,stems,why));
    puts("offline stems isolate each audio output");

    // Cancellation never leaves a partial result that a caller could save.
    std::atomic<bool> cancelled{true};
    assert(!offline::render(graph,1,48000,result,why,&cancelled));
    assert(result.left.empty() && result.right.empty() && result.midi.empty());
    assert(!offline::renderStems(stemGraph,.25,48000,stems,why,&cancelled));
    assert(stems.empty());
    cancelled=false;
    assert(offline::render(graph,.1,48000,result,why,&cancelled));
    assert(result.left.size()==4800 && result.peak>.01);
    puts("cancelled exports clear partial audio, MIDI and stems and can restart");

    assert(!offline::render(graph,0,48000,result,why));
    assert(!offline::render(graph,200,48000,result,why));
    puts("invalid export durations are rejected");
    puts("PASS: offline rendering, stems, WAV/multi-track MIDI export and MIDI capture.");
}
