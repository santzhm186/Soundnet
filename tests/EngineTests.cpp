#include "../src/Engine.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <numeric>
#include <vector>

using namespace tri;
constexpr int sr = 48000;
struct Block {
    std::vector<float> l,r;
    explicit Block(int frames) : l(frames),r(frames) {}
};
Block run(Engine& e, int frames, const float* left=nullptr,const float* right=nullptr) {
    Block b(frames);
    e.render(left,right,b.l.data(),b.r.data(),frames);
    return b;
}
double rms(const std::vector<float>& values,int begin,int end) {
    double sum=0;
    for (int i=begin; i<end; ++i) sum+=values[i]*values[i];
    return std::sqrt(sum/(end-begin));
}
void single(Engine& e) {
    e.set(DelayMix,0); e.set(Master,1);
    e.set(Level1,1); e.set(Level2,0); e.set(Level3,0);
    e.set(Wave1,0); e.set(Tune1,0); e.set(Attack,.001); e.set(Decay,.005);
    e.set(Sustain,1); e.set(Release,.005); e.prepare(sr);
}
double harmonic(const std::vector<float>& sample,int start,int count,double hz) {
    double re=0,im=0;
    for (int i=0;i<count;++i) {
        const double phase=6.283185307179586*hz*i/sr;
        re+=sample[start+i]*std::cos(phase); im+=sample[start+i]*std::sin(phase);
    }
    return 2*std::hypot(re,im)/count;
}
void oscillatorTest() {
    Engine e; single(e);
    for (int osc=0; osc<3; ++osc) {
        for (int i=0; i<3; ++i) { e.set(Level1+3*i,i==osc); e.set(Tune1+3*i,0); }
        for (int shape=0; shape<4; ++shape) {
            e.set(Wave1+osc*3,shape); e.reset(); e.midi(0x91,69,127);
            const auto sound=run(e,sr/2).l;
            const double f=harmonic(sound,4800,9600,440);
            const double second=harmonic(sound,4800,9600,880)/f;
            const double third=harmonic(sound,4800,9600,1320)/f;
            assert(f>.14 && f<.33);
            if (shape==0) assert(second<.002 && third<.002);
            if (shape==1) assert(second<.002 && std::abs(third-1./3)<.015);
            if (shape==2) assert(std::abs(second-.5)<.01);
            if (shape==3) assert(second<.002 && std::abs(third-1./9)<.01);
        }
    }
    single(e);
    e.set(Tune1,12); e.midi(0x90,69,64); auto sound=run(e,18000).l;
    assert(harmonic(sound,4800,9600,880)>.12);
    assert(harmonic(sound,4800,9600,440)<.001);
}
void midiTest() {
    Engine e; single(e);
    e.midi(0x90,57,127); e.midi(0x91,69,64);
    auto chord=run(e,18000).l;
    assert(harmonic(chord,4800,9600,220)>.22 && harmonic(chord,4800,9600,440)>.12);
    e.midi(0xb0,64,127); e.midi(0x80,57,0); e.midi(0x91,69,0);
    auto sustain=run(e,18000).l;
    assert(harmonic(sustain,4800,9600,220)>.22 && harmonic(sustain,4800,9600,440)<.001);
    e.midi(0xe0,127,127);
    auto bent=run(e,18000).l;
    assert(harmonic(bent,4800,9600,246.94)>.22);
    e.midi(0xb0,64,0); auto tail=run(e,18000).l;
    assert(rms(tail,1000,18000)<1e-7);
}
void arpTest() {
    Engine e;
    for(int pattern=0;pattern<5;++pattern) {
        single(e); e.set(ArpOn,1); e.set(Pattern,pattern); e.set(Gate,.30f);
        e.set(Tempo,86); e.setHostTempo(120);
        e.midi(0x90,69,127); e.midi(0x90,81,127);
        auto b=run(e,96000).l;
        int hit=0;
        for(int step=0;step<16;++step) {
            const int start=step*6000+120;
            bool expected=((patternMasks[pattern]>>step)&1)!=0;
            assert((rms(b,start,start+800)>.025)==expected);
            if (expected) {
                double fA=harmonic(b,start,1000,440),fB=harmonic(b,start,1000,880);
                assert((fA>fB)==((hit%2)==0));
                ++hit;
            }
        }
        e.midi(0x80,81,0); e.midi(0x80,69,0);
        auto off=run(e,12000).l;
        assert(rms(off,600,12000)<1e-6);
    }
    single(e); e.set(ArpOn,1); e.set(Pattern,2); e.setHostTempo(0); e.set(Tempo,60);
    e.midi(0x90,69,120);
    auto reggae=run(e,28000).l;
    assert(rms(reggae,0,23999)<1e-6 && rms(reggae,24200,24900)>.05);
    e.set(ArpOn,0); auto direct=run(e,18000).l;
    assert(harmonic(direct,4800,9600,440)>.20);
    e.midi(0xb0,120,0); assert(rms(run(e,2000).l,0,2000)<1e-7);
}
// Separate sine partials make each oscillator's envelope measurable in a single voice.
void independentEnvelopeTest() {
    Engine e; single(e);
    const double frequencies[] = {220,440,880};
    const float attacks[] = {0,.4f,.8f};
    const float sustains[] = {0,.3f,.8f};
    const float releases[] = {0,.1f,1.2f};
    for (int osc=0; osc<3; ++osc) {
        const int offset = 3*osc;
        e.set(Wave1+offset,0); e.set(Tune1+offset,12*osc); e.set(Level1+offset,1);
        assert(e.get(Osc1Attack+offset)==Follow);
        assert(e.get(Osc1Sustain+offset)==Follow);
        assert(e.get(Osc1Release+offset)==Follow);
        e.set(Osc1Attack+offset,attacks[osc]);
        e.set(Osc1Release+offset,releases[osc]);
    }
    e.midi(0x90,57,127);
    auto onset=run(e,sr).l;
    for (int osc=0; osc<3; ++osc) {
        const double level=harmonic(onset,2400,2400,frequencies[osc]);
        const double expected=osc==0 ? .25 : .25*.075/attacks[osc];
        assert(std::abs(level-expected)<.002);
        e.set(Osc1Sustain+3*osc,sustains[osc]);
    }
    auto held=run(e,4800).l;
    for (int osc=0; osc<3; ++osc) {
        assert(std::abs(harmonic(held,0,4800,frequencies[osc])-.25*sustains[osc])<.001);
        e.set(Osc1Sustain+3*osc,-.2f);
        assert(e.get(Osc1Sustain+3*osc)==Follow);
    }
    // Returning one override to Follow makes it respond to subsequent shared edits.
    e.set(Sustain,.84f);
    auto restored=run(e,4800).l;
    for (auto hz : frequencies) assert(std::abs(harmonic(restored,0,4800,hz)-.21)<.001);
    e.midi(0x80,57,0);
    auto tail=run(e,sr*14/10).l;
    assert(harmonic(tail,0,2400,220)<.003);
    assert(std::abs(harmonic(tail,0,2400,440)-.21*.75)<.003);
    assert(harmonic(tail,12000,2400,220)<.001 && harmonic(tail,12000,2400,440)<.001);
    assert(std::abs(harmonic(tail,12000,2400,880)-.21*(1-.275/1.2))<.001);
    assert(rms(tail,58000,65000)<1e-7);
}

// Verify that only audible triggers count toward Repeat, including across sparse bars.
void arpRepeatTest() {
    Engine e; single(e);
    assert(e.get(ArpRepeat)==1);
    e.set(ArpRepeat,8.7f); assert(e.get(ArpRepeat)==8);
    e.set(ArpRepeat,0); assert(e.get(ArpRepeat)==1);
    e.set(ArpOn,1); e.set(Gate,.20f); e.setHostTempo(120);
    for (int pattern : {1,2,4}) {
        for (int repeats : {1,3,8}) {
            e.reset(); e.set(Pattern,pattern); e.set(ArpRepeat,repeats+.2f);
            e.midi(0x90,69,127); e.midi(0x90,81,127);
            int hit=0;
            for (int step=0;step<48;++step) {
                auto b=run(e,6000).l;
                const bool shouldHit=(patternMasks[pattern]>>(step%16))&1;
                assert((rms(b,160,1000)>.02)==shouldHit);
                assert(rms(b,3300,5900)<1e-7);
                if (!shouldHit) continue;
                const double fA=harmonic(b,100,900,440),fB=harmonic(b,100,900,880);
                assert((fA>fB)==((hit/repeats)%2==0));
                ++hit;
            }
        }
    }
    e.reset(); e.set(Pattern,4); e.set(ArpRepeat,8);
    for (int note : {57,69,81}) e.midi(0x90,note,127);
    run(e,6000);
    e.midi(0x80,57,0); // Released notes may not be repeated.
    auto next=run(e,6000).l;
    assert(harmonic(next,100,900,440)>.20);
    e.set(ArpRepeat,1); // Shortening the run takes effect on the next trigger.
    auto afterEdit=run(e,6000).l;
    assert(harmonic(afterEdit,100,900,880)>.20);
    e.midi(0xb0,120,0); run(e,2000);
    e.set(ArpRepeat,3); e.midi(0x90,57,127); e.midi(0x90,69,127);
    auto restarted=run(e,6000).l;
    assert(harmonic(restarted,100,900,220)>.20);
}

void effectsTest() {
    Engine e; single(e); e.set(InputGain,1);
    std::vector<float> left(8000),right(8000);
    left[0]=.5; right[4]=-.3;
    e.render(left.data(),right.data(),left.data(),right.data(),8000);
    assert(std::abs(left[0]-.5)<1e-6 && std::abs(right[4]+.3)<1e-6 && right[0]==0);
    e.set(DelayTime,50); e.set(DelayFeedback,.5); e.set(DelayMix,1); e.reset();
    auto out=run(e,8000,left.data(),right.data());
    assert(out.l[0]==0 && std::abs(out.l[2400]-.5)<1e-6 && std::abs(out.l[4800]-.25)<1e-6);
    assert(std::abs(out.r[2404]+.3)<1e-6 && std::abs(out.r[4804]+.15)<1e-6);
    e.set(DelayMix,0);
    for (int band=0; band<3; ++band) {
        const double frequencies[]={35,1000,14000};
        std::vector<float> input(20000);
        for(int i=0;i<20000;++i) input[i]=.05f*std::sin(6.283185307179586*frequencies[band]*i/sr);
        for (int gain : {-9,9}) {
            for(int j=0;j<3;++j) e.set(EQLow+j,j==band?gain:0);
            e.reset();
            auto output=run(e,20000,input.data());
            double db=20*std::log10(rms(output.l,3000,20000)/rms(input,3000,20000));
            assert(std::abs(db-gain)<.5);
            assert(output.l==output.r);
        }
    }
}
void patchGraphTest() {
    Engine e;
    single(e);
    assert(validRouting(defaultRouting) && e.routing()==defaultRouting);
    assert(!canConnect(defaultRouting,RouteEQIn,DelayNode));
    assert(!canConnect(defaultRouting,RouteMix1,MixNode));
    assert(!canConnect(defaultRouting,RouteOutputIn,ArpNode));
    assert(!canConnect(defaultRouting,RouteOsc2In,InputNode));
    assert(!canConnect(defaultRouting,RouteArpIn,ArpNode));
    assert(!canConnect(defaultRouting,RouteMix1,OutputNode));
    assert(!canConnect(defaultRouting,Master,InputNode));
    assert(!canConnect(defaultRouting,RouteMix4,32));
    e.set(RouteEQIn,DelayNode);
    assert(e.get(RouteEQIn)==MixNode);
    auto invalid=defaultRouting;
    invalid[RouteMix2-RouteArpIn]=DelayNode;
    assert(!e.setRouting(invalid) && e.routing()==defaultRouting);

    // A live held voice goes to the selected audio source at the next buffer.
    e.set(Level2,1); e.set(Wave2,0); e.set(Tune2,12);
    e.midi(0x90,57,127);
    e.set(RouteOutputIn,Osc1Node);
    auto osc=run(e,12000).l;
    assert(harmonic(osc,4800,4800,220)>.24 && harmonic(osc,4800,4800,440)<.001);
    e.set(RouteOutputIn,Osc2Node);
    osc=run(e,12000).l;
    assert(harmonic(osc,4800,4800,440)>.24 && harmonic(osc,4800,4800,220)<.001);
    e.set(RouteOutputIn,NoNode); assert(rms(run(e,480).l,0,480)==0);
    e.midi(0xb0,120,0);

    // Feed the EQ from a delayed input and mix its return alongside the dry
    // source. This requires Delay -> EQ -> Mix, the reverse of factory order.
    Routing parallel{};
    parallel[RouteDelayIn-RouteArpIn]=InputNode;
    parallel[RouteEQIn-RouteArpIn]=DelayNode;
    parallel[RouteMix1-RouteArpIn]=EQNode;
    parallel[RouteMix2-RouteArpIn]=InputNode;
    parallel[RouteOutputIn-RouteArpIn]=MixNode;
    assert(e.setRouting(parallel));
    e.set(DelayTime,50); e.set(DelayFeedback,0); e.set(DelayMix,1); e.reset();
    std::vector<float> left(6000),right(6000);
    left[3]=.2f; right[9]=-.12f;
    const auto echo=run(e,6000,left.data(),right.data());
    for(int i=0;i<6000;++i) {
        assert(std::abs(echo.l[i]-(i==3||i==2403?.2f:0))<1e-5f);
        assert(std::abs(echo.r[i]-(i==9||i==2409?-.12f:0))<1e-5f);
    }
    e.set(RouteOutputIn,InputNode);
    auto dry=run(e,6000,left.data(),right.data()); assert(dry.l==left && dry.r==right);

    // ARP releases must only release oscillators fed by the ARP. Reggae starts
    // with a rest while the oscillator wired to MIDI plays the whole phrase.
    Routing split=defaultRouting;
    split[RouteOsc1In-RouteArpIn]=MidiNode;
    assert(e.setRouting(split));
    e.set(ArpOn,1); e.set(Pattern,2); e.set(Gate,.20f); e.set(DelayMix,0);
    e.setHostTempo(120); e.reset(); e.midi(0x90,57,127);
    auto phrase=run(e,23800).l;
    assert(harmonic(phrase,4800,4800,220)>.24 && harmonic(phrase,4800,4800,440)<.001);
    assert(harmonic(phrase,12200,1800,220)>.23 && harmonic(phrase,12200,1800,440)>.23);
    assert(harmonic(phrase,17000,4800,220)>.24 && harmonic(phrase,17000,4800,440)<.001);
    e.set(RouteArpIn,NoNode);
    auto disconnected=run(e,24000).l;
    assert(harmonic(disconnected,12000,4800,220)>.24 && harmonic(disconnected,12000,4800,440)<.001);
    e.set(RouteOsc1In,NoNode);
    auto silent=run(e,6000).l;
    assert(rms(silent,1000,6000)<1e-7);
    e.set(RouteOsc2In,MidiNode);
    auto reconnected=run(e,10000).l;
    assert(harmonic(reconnected,4800,4800,440)>.24);
    e.midi(0x80,57,0);
    assert(rms(run(e,6000).l,1000,6000)<1e-7);
}

int main() {
    oscillatorTest(); midiTest(); arpTest(); effectsTest();
    independentEnvelopeTest(); arpRepeatTest(); patchGraphTest();
    std::puts("PASS: four spectra on each oscillator, tuning, velocity, MIDI channels, sustain and bend, five rhythm masks, host/local tempo, arp handoff, in-place stereo input, delay echoes and three EQ bands. Independent envelopes, inheritance and long tails; repeated ARP pitches, rests, gates and chord edits. Typed DAG validation, reordered serial/parallel effects, stereo fan-out, output selection and mixed MIDI/ARP triggering.");
}
