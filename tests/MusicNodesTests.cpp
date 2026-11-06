#include "../src/Engine.hpp"
#include <cassert>
#include <iostream>
#include <memory>
double spectrum(const std::vector<float>& audio,double hz,int from=12000) {
    double re=0,im=0;int size=4800;
    for(int i=0;i<size;++i){double phase=2*3.141592653589793*hz*i/48000;re+=audio[from+i]*cos(phase);im+=audio[from+i]*sin(phase);}
    return hypot(re,im)/size;
}
int main(){
    auto chord=tri::parseChord("F#m7");assert(chord && chord->root==66 && chord->quality==4);
    assert(tri::parseProgression("Cmaj7 | Am7 | Dm7 | G7")->size()==4);
    assert(!tri::parseProgression("C | what") && !tri::parseProgression("C | C | C | C | C | C | C | C | C"));
    auto synth=std::make_unique<tri::Engine>();auto& engine=*synth;
    engine.set(tri::Wave1,0);engine.set(tri::Level1,1);engine.set(tri::Level2,0);engine.set(tri::Level3,0);
    engine.set(tri::Sustain,1);engine.set(tri::Attack,.001);engine.set(tri::Decay,.005);engine.set(tri::DelayMix,0);engine.set(tri::Master,1);
    auto routing=tri::defaultRouting;
    routing[tri::RouteOutputIn-tri::RouteArpIn]=tri::Osc1Node;
    routing[tri::RouteOsc1In-tri::RouteArpIn]=tri::ChordNode;
    assert(engine.setRouting(routing));
    engine.set(tri::ChordRun,1);engine.set(tri::ChordRoot,60);engine.set(tri::ChordQuality,3);
    std::vector<float> left(120000),right(left.size());
    engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
    for(double hz:{261.625565,329.627557,391.995436,493.883301})assert(spectrum(left,hz)>.035);
    engine.reset();engine.set(tri::ChordRun,0);engine.set(tri::ProgressRun,1);
    engine.set(tri::ClockBPM,240);engine.set(tri::ClockSync,0);engine.set(tri::ProgressLength,2);
    engine.set(tri::ProgressRoot1,60);engine.set(tri::ProgressQuality1,0);
    engine.set(tri::ProgressRoot2,62);engine.set(tri::ProgressQuality2,0);
    routing[tri::RouteOsc1In-tri::RouteArpIn]=tri::ProgressNode;
    routing[tri::RouteProgressClock-tri::RouteArpIn]=tri::ClockNode;assert(engine.setRouting(routing));
    engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
    assert(spectrum(left,261.625565,12000)>.035);
    assert(spectrum(left,293.664768,60000)>.035);
    std::cout<<"C residual "<<spectrum(left,261.625565,60000)<<" D "<<spectrum(left,293.664768,60000)<<"\n";
    assert(spectrum(left,293.664768,60000)>5*spectrum(left,261.625565,60000));
    engine.reset();engine.set(tri::ProgressRun,0);engine.set(tri::Tempo,120);engine.set(tri::Release,.005);
    engine.set(tri::VelocityMin,20);engine.set(tri::VelocityMax,120);engine.set(tri::VelocityRate,4);
    routing[tri::RouteOsc1In-tri::RouteArpIn]=tri::VelocityNode;
    routing[tri::RouteVelocityIn-tri::RouteArpIn]=tri::MidiNode;
    routing[tri::RouteVelocityClock-tri::RouteArpIn]=tri::NoNode;
    assert(engine.setRouting(routing));
    engine.midi(0x90,69,100);engine.render(nullptr,nullptr,left.data(),right.data(),48000);
    double soft=spectrum(left,440);
    engine.midi(0x80,69,0);engine.midi(0x90,69,100);
    engine.render(nullptr,nullptr,left.data(),right.data(),48000);
    double loud=spectrum(left,440);assert(loud>soft*4);
    auto cyclic=routing;
    cyclic[tri::RouteArpIn-tri::RouteArpIn]=tri::VelocityNode;
    cyclic[tri::RouteVelocityIn-tri::RouteArpIn]=tri::ArpNode;
    assert(!tri::validRouting(cyclic));
    std::cout<<"PASS: custom chord parsing, four sounding Cmaj7 notes, clocked chord progression, velocity automation and note-cycle protection.\n";
}
