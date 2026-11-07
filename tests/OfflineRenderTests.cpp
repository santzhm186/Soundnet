#include "../src/OfflineRender.hpp"
#include "../src/WavFile.hpp"
#include "../src/AudioFile.hpp"
#include "../src/MidiFileWriter.hpp"
#include "../src/StandardMidiFile.hpp"
#include <cassert>
#include <fstream>
#include <cstdio>
#include <string>

using namespace tri;

static void edge(GraphSpec& graph,uint64_t source,uint64_t target,int port=0,int sourcePort=0) {
    graph.edges.push_back({source,target,port,sourcePort});
}

int main() {
    GraphSpec graph;
    GraphNode start;start.id=1;start.type=StartNode;start.x=0;start.y=0;graph.nodes.push_back(start);
    GraphNode clip;clip.id=2;clip.type=ClipNode;clip.x=120;clip.y=0;
    clip.clip.lengthBeats=2;
    clip.clip.events={{0,0x90,60,100},{1,0x80,60,0}};
    graph.nodes.push_back(clip);
    GraphNode osc;osc.id=3;osc.type=Osc1Node;osc.x=240;osc.y=0;osc.parameters[Level1]=.6f;graph.nodes.push_back(osc);
    GraphNode out;out.id=4;out.type=OutputNode;out.x=360;out.y=0;out.parameters[Master]=1;graph.nodes.push_back(out);
    edge(graph,1,2,1);edge(graph,2,3);edge(graph,3,4);
    std::vector<size_t> order;assert(graphOrder(graph,order));

    offline::Result result;std::string why;
    assert(offline::render(graph,1,48000,result,why));
    assert(result.left.size()==48000 && result.right.size()==48000);
    double peak=0;for(float value:result.left)peak=std::max(peak,double(std::fabs(value)));
    assert(peak>.01);
    assert(result.peak>.01);
    puts("start-triggered graph renders offline");

    GraphSpec midiGraph;
    GraphNode midiStart;midiStart.id=1;midiStart.type=StartNode;midiGraph.nodes.push_back(midiStart);
    GraphNode midiClip;midiClip.id=2;midiClip.type=ClipNode;midiClip.clip.lengthBeats=1;
    midiClip.clip.events={{0,0x90,67,110},{.5,0x80,67,0}};midiGraph.nodes.push_back(midiClip);
    GraphNode midiOut;midiOut.id=3;midiOut.type=MidiOutNode;midiGraph.nodes.push_back(midiOut);
    edge(midiGraph,1,2,1);edge(midiGraph,2,3);
    GraphNode midiClock;midiClock.id=4;midiClock.type=ClockNode;midiClock.parameters[ClockBPM]=90;
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
    assert(midiResult.bpm==90);
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

    GraphSpec daw;
    GraphNode dawStart;dawStart.id=1;dawStart.type=StartNode;daw.nodes.push_back(dawStart);
    GraphNode dawClip;dawClip.id=2;dawClip.type=ClipNode;dawClip.clip.lengthBeats=2;
    dawClip.clip.events={{0,0x90,64,100},{1,0x80,64,0}};daw.nodes.push_back(dawClip);
    GraphNode dawOsc;dawOsc.id=3;dawOsc.type=Osc1Node;daw.nodes.push_back(dawOsc);
    GraphNode dawOut;dawOut.id=4;dawOut.type=DawOutputNode;dawOut.parameters[Master]=1;daw.nodes.push_back(dawOut);
    edge(daw,1,2,1);edge(daw,2,3);edge(daw,3,4);
    offline::Result redirected;
    assert(offline::render(daw,1,48000,redirected,why));
    double redirectedPeak=0;for(float value:redirected.left)redirectedPeak=std::max(redirectedPeak,double(std::fabs(value)));
    assert(redirectedPeak>.01);
    puts("DAW-only graphs export through the hardware path");

    offline::Result rateResult;
    assert(offline::render(graph,1,44100,rateResult,why));
    assert(std::abs(rateResult.rate-44100)<.1 && rateResult.left.size()==44100);
    puts("44.1 kHz offline rendering is supported");

    assert(!offline::render(graph,0,48000,result,why));
    assert(!offline::render(graph,200,48000,result,why));
    puts("invalid export durations are rejected");
    puts("PASS: offline rendering, WAV/multi-track MIDI export, MIDI capture and output redirection.");
}
