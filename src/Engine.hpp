#pragma once
#include "Harmony.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tri {

enum Param {
    Wave1, Level1, Tune1, Wave2, Level2, Tune2, Wave3, Level3, Tune3,
    Attack, Decay, Sustain, Release, InputGain, DelayTime, DelayFeedback,
    DelayMix, EQLow, EQMid, EQHigh, ArpOn, Pattern, Tempo, Gate, Master,
    Osc1Attack, Osc1Sustain, Osc1Release, Osc2Attack, Osc2Sustain, Osc2Release,
    Osc3Attack, Osc3Sustain, Osc3Release, ArpRepeat,
    RouteArpIn, RouteOsc1In, RouteOsc2In, RouteOsc3In, RouteMix1, RouteMix2,
    RouteMix3, RouteMix4, RouteEQIn, RouteDelayIn, RouteOutputIn,
    RouteChordIn, RouteVelocityIn, RouteArpClock, RouteProgressClock, RouteVelocityClock,
    ChordRoot, ChordQuality, ChordRun, ProgressRun, ProgressLength, ProgressBars,
    ClockBPM, ClockSync, VelocityMin, VelocityMax, VelocityRate, VelocityShape,
    ProgressRoot1, ProgressQuality1, ProgressRoot2, ProgressQuality2,
    ProgressRoot3, ProgressQuality3, ProgressRoot4, ProgressQuality4,
    ProgressRoot5, ProgressQuality5, ProgressRoot6, ProgressQuality6,
    ProgressRoot7, ProgressQuality7, ProgressRoot8, ProgressQuality8, ParamCount
};

enum Node {
    NoNode, MidiNode, ArpNode, Osc1Node, Osc2Node, Osc3Node,
    InputNode, MixNode, EQNode, DelayNode, OutputNode, ChordNode, ProgressNode, ClockNode, VelocityNode
};
inline constexpr const char* nodeNames[] = {
    "Disconnected", "MIDI", "ARP", "OSC 1", "OSC 2", "OSC 3",
    "Audio Input", "Mixer", "EQ", "Delay", "Output", "Chord", "Progression", "Clock", "Velocity"
};
using Routing = std::array<int,RouteVelocityClock-RouteArpIn+1>;
inline constexpr Routing defaultRouting = {
    MidiNode, ArpNode, ArpNode, ArpNode, Osc1Node, Osc2Node,
    Osc3Node, InputNode, MixNode, EQNode, DelayNode, NoNode, NoNode, NoNode, NoNode, NoNode
};
inline constexpr Node routeTarget(int param) {
    constexpr Node targets[] = {ArpNode,Osc1Node,Osc2Node,Osc3Node,MixNode,MixNode,
                                MixNode,MixNode,EQNode,DelayNode,OutputNode,ChordNode,VelocityNode,ArpNode,ProgressNode,VelocityNode};
    return param>=RouteArpIn && param<=RouteVelocityClock ? targets[param-RouteArpIn] : NoNode;
}
inline bool noteSource(int n) { return n==MidiNode || n==ArpNode || n==ChordNode || n==ProgressNode || n==VelocityNode; }
inline bool clockPort(int p) { return p>=RouteArpClock && p<=RouteVelocityClock; }
inline constexpr bool routeParam(int p) {return p>=RouteArpIn && p<=RouteVelocityClock;}
inline bool compatibleSource(int param,int source) {
    if(!routeParam(param))return false;
    if(!source)return true;
    if(clockPort(param))return source==ClockNode;
    if(param<=RouteOsc3In || param==RouteChordIn || param==RouteVelocityIn)
        return noteSource(source) && source!=routeTarget(param);
    return source>=Osc1Node && source<=DelayNode && source!=routeTarget(param);
}
// Only Mixer, EQ and Delay can have dependencies on other audio processors.
// Emit all three in an order in which every source is already available.
inline bool processingOrder(const Routing& r, std::array<Node,3>& order) {
    int remaining = (1<<MixNode) | (1<<EQNode) | (1<<DelayNode);
    int count = 0;
    for (int pass=0; pass<3 && remaining; ++pass) {
        for (Node node : {MixNode,EQNode,DelayNode}) {
            if (!(remaining & (1<<node))) continue;
            const int first = node==MixNode ? RouteMix1 : (node==EQNode ? RouteEQIn : RouteDelayIn);
            const int last = node==MixNode ? RouteMix4 : first;
            bool ready = true;
            for (int param=first; param<=last; ++param) {
                const int source = r[param-RouteArpIn];
                if (source<0 || source>DelayNode || (remaining & (1<<source))) ready = false;
            }
            if (!ready) continue;
            order[count++] = node;
            remaining &= ~(1<<node);
        }
    }
    return remaining == 0;
}
inline bool validRouting(const Routing& r) {
    for (int i=RouteArpIn; i<=RouteVelocityClock; ++i)
        if (!compatibleSource(i,r[i-RouteArpIn])) return false;
    std::array<Node,3> order;
    if(!processingOrder(r,order))return false;
    for(int origin : {ArpNode,ChordNode,VelocityNode}) {
        int node=origin;
        for(int depth=0;depth<5;++depth) {
            int input=node==ArpNode?RouteArpIn:node==ChordNode?RouteChordIn:node==VelocityNode?RouteVelocityIn:-1;
            if(input<0)break;
            node=r[input-RouteArpIn];
            if(node==origin)return false;
        }
    }
    return true;
}
inline bool canConnect(const Routing& r, int param, int source) {
    if (!compatibleSource(param,source)) return false;
    auto proposed = r;
    proposed[param-RouteArpIn] = source;
    return validRouting(proposed);
}

inline constexpr uint64_t packRouting(const Routing& r) {
    uint64_t bits = 0;
    for (size_t i=0; i<r.size(); ++i)
        bits |= (static_cast<uint64_t>(r[i]) & 15u) << (i*4);
    return bits;
}
inline constexpr Routing unpackRouting(uint64_t bits) {
    Routing r{};
    for (size_t i=0; i<r.size(); ++i) r[i] = static_cast<int>((bits>>(i*4)) & 15u);
    return r;
}

// Older sessions only serialize parameters through Master or ArpRepeat.
inline constexpr float Follow = -1.f;

struct ParamInfo {
    const char* name;
    float min, max, def;
    const char* unit;
};

inline constexpr const char* waveNames[] = {"Sine", "Square", "Saw", "Triangle"};
inline constexpr const char* patternNames[] = {
    "Straight 8ths", "Bossa Nova", "Reggae", "Disco", "Pulse 16ths"
};
// Bit 0 is the downbeat of the bar, one bit per sixteenth note.
inline constexpr uint16_t patternMasks[] = {0x5555, 0x2449, 0x4444, 0x6565, 0xffff};

inline constexpr ParamInfo params[ParamCount] = {
    {"Osc 1 Wave",0,3,2,""}, {"Osc 1 Level",0,1,.66f,""}, {"Osc 1 Tune",-24,24,0,"st"},
    {"Osc 2 Wave",0,3,3,""}, {"Osc 2 Level",0,1,.38f,""}, {"Osc 2 Tune",-24,24,-12,"st"},
    {"Osc 3 Wave",0,3,0,""}, {"Osc 3 Level",0,1,.18f,""}, {"Osc 3 Tune",-24,24,12,"st"},
    {"Attack",.001f,3,.012f,"s"}, {"Decay",.005f,3,.22f,"s"},
    {"Sustain",0,1,.70f,""}, {"Release",.005f,5,.24f,"s"},
    {"Audio Input",0,1,1,""}, {"Delay Time",20,1000,310,"ms"},
    {"Delay Feedback",0,.88f,.32f,""}, {"Delay Mix",0,1,.12f,""},
    {"EQ Low",-12,12,0,"dB"}, {"EQ Mid",-12,12,0,"dB"}, {"EQ High",-12,12,0,"dB"},
    {"Arpeggiator",0,1,0,""}, {"Rhythm",0,4,0,""}, {"Tempo",40,240,112,"BPM"},
    {"Gate",.08f,.98f,.60f,""}, {"Master",0,1,.74f,""},
    {"Osc 1 Attack",Follow,3,Follow,"s"}, {"Osc 1 Sustain",Follow,1,Follow,""}, {"Osc 1 Release",Follow,5,Follow,"s"},
    {"Osc 2 Attack",Follow,3,Follow,"s"}, {"Osc 2 Sustain",Follow,1,Follow,""}, {"Osc 2 Release",Follow,5,Follow,"s"},
    {"Osc 3 Attack",Follow,3,Follow,"s"}, {"Osc 3 Sustain",Follow,1,Follow,""}, {"Osc 3 Release",Follow,5,Follow,"s"},
    {"Arp Repeat",1,8,1,""},
    {"ARP Note Source",0,1,MidiNode,""}, {"Osc 1 Note Source",0,2,ArpNode,""},
    {"Osc 2 Note Source",0,2,ArpNode,""}, {"Osc 3 Note Source",0,2,ArpNode,""},
    {"Mix Input 1",0,9,Osc1Node,""}, {"Mix Input 2",0,9,Osc2Node,""},
    {"Mix Input 3",0,9,Osc3Node,""}, {"Mix Input 4",0,9,InputNode,""},
    {"EQ Source",0,9,MixNode,""}, {"Delay Source",0,9,EQNode,""}, {"Output Source",0,9,DelayNode,""},
    {"Chord Note Source",0,14,0,""}, {"Velocity Note Source",0,14,0,""},
    {"ARP Clock",0,13,0,""}, {"Progression Clock",0,13,0,""}, {"Velocity Clock",0,13,0,""},
    {"Chord Root",24,96,60,"MIDI"}, {"Chord Quality",0,7,3,""}, {"Chord Play",0,1,0,""},
    {"Progression Play",0,1,0,""}, {"Progression Length",1,8,4,""}, {"Bars Per Chord",1,8,1,""},
    {"Clock BPM",40,240,112,"BPM"}, {"Clock Host Sync",0,1,1,""},
    {"Velocity Minimum",1,127,48,""}, {"Velocity Maximum",1,127,112,""},
    {"Velocity Beats",.25f,16,4,"beats"}, {"Velocity Shape",0,2,0,""},
    {"Progress Root 1",24,96,60,""}, {"Progress Quality 1",0,7,3,""},
    {"Progress Root 2",24,96,69,""}, {"Progress Quality 2",0,7,4,""},
    {"Progress Root 3",24,96,62,""}, {"Progress Quality 3",0,7,4,""},
    {"Progress Root 4",24,96,67,""}, {"Progress Quality 4",0,7,2,""},
    {"Progress Root 5",24,96,60,""}, {"Progress Quality 5",0,7,0,""},
    {"Progress Root 6",24,96,60,""}, {"Progress Quality 6",0,7,0,""},
    {"Progress Root 7",24,96,60,""}, {"Progress Quality 7",0,7,0,""},
    {"Progress Root 8",24,96,60,""}, {"Progress Quality 8",0,7,0,""}
};

class Engine {
    static constexpr double pi = 3.14159265358979323846;
    static constexpr int tableSize = 2048;
    enum Stage { idle, attack, decay, sustain, release };
    struct Envelope {
        Stage stage = idle;
        float level = 0, releaseFrom = 0;
        double progress = 0;
    };
    struct Voice {
        bool active = false, releasing = false, fromArp = false;
        unsigned oscillators = 0;
        int channel = 0, note = 0;
        uint64_t age = 0;
        float gain = 0;
        double hz = 0;
        std::array<double, 3> phase{};
        std::array<Envelope, 3> envelopes{};
        float peakEnvelope() const {
            return std::max({envelopes[0].level, envelopes[1].level, envelopes[2].level});
        }
    };
    struct Key {
        unsigned char down = 0, velocity = 0;
    };
    struct Filter {
        double b0 = 1,b1 = 0,b2 = 0,a1 = 0,a2 = 0;
        std::array<double,2> z1{},z2{};

        float tick(float input, int channel) {
            const double out = b0*input + z1[channel];
            z1[channel] = b1*input - a1*out + z2[channel];
            z2[channel] = b2*input - a2*out;
            if (std::abs(z1[channel]) < 1e-22) z1[channel] = 0;
            if (std::abs(z2[channel]) < 1e-22) z2[channel] = 0;
            return static_cast<float>(out);
        }

        // RBJ low shelf at 180 Hz, bell at 1 kHz, high shelf at 5 kHz.
        void configure(int band, float db, double sampleRate) {
            const double fc[] = {180,1000,5000};
            const double omega = 2*pi*std::min(fc[band],sampleRate*.42)/sampleRate;
            const double c = std::cos(omega), s = std::sin(omega), A = std::pow(10., db/40.);
            const double plus = A+1, minus = A-1, beta = std::sqrt(2*A)*s;
            double a0;
            if (band == 0) {
                b0 = A*(plus-minus*c+beta); b1 = 2*A*(minus-plus*c); b2 = A*(plus-minus*c-beta);
                a0 = plus+minus*c+beta; a1 = -2*(minus+plus*c); a2 = plus+minus*c-beta;
            } else if (band == 1) {
                const double alpha = s/(2*.8);
                b0 = 1+alpha*A; b1 = -2*c; b2 = 1-alpha*A;
                a0 = 1+alpha/A; a1 = -2*c; a2 = 1-alpha/A;
            } else {
                b0 = A*(plus+minus*c+beta); b1 = -2*A*(minus+plus*c); b2 = A*(plus+minus*c-beta);
                a0 = plus-minus*c+beta; a1 = 2*(minus-plus*c); a2 = plus-minus*c-beta;
            }
            b0/=a0; b1/=a0; b2/=a0; a1/=a0; a2/=a0;
        }
    };

    static_assert(std::atomic<float>::is_always_lock_free, "Audio parameters must be lock-free");
    static_assert(std::atomic<uint64_t>::is_always_lock_free, "Audio routing must be lock-free");
    std::atomic<uint64_t> routeBits{packRouting(defaultRouting)};
    uint64_t appliedBits = packRouting(defaultRouting);
    Routing routes = defaultRouting;
    std::array<Node,3> order = {MixNode,EQNode,DelayNode};
    unsigned directMask = 0, arpMask = 0;
    std::array<std::atomic<float>,ParamCount> values;
    std::array<float,ParamCount> p{};
    std::array<float,tableSize+1> sine{};
    std::array<double,128> frequency{};
    std::array<double,3> transpose{};
    std::array<double,16> bend{};
    std::array<bool,16> pedal{};
    std::array<std::array<Key,128>,16> keys{};
    std::array<Voice,24> voices{};
    std::array<Filter,3> filters{};
    std::array<std::vector<float>,2> echo;
    int writeIndex = 0;
    double delayCurrent = 0, sr = 48000, hostTempo = 0;
    uint64_t serial = 0;
    int heldCount = 0, lastPitchKey = -1, arpStep = 0, arpChannel = -1, arpPitch = -1;
    int hitsOnPitch = 0;
    double untilStep = 0, untilRelease = 0;

    struct MusicNote { uint8_t velocity=0; uint64_t token=0; };
    using NoteSet=std::array<MusicNote,2048>;
    std::array<NoteSet,15> musicNotes{};
    NoteSet rootNotes{};
    std::array<NoteSet,3> soundingNotes{};
    bool musicMode=false, wasChordRunning=false, wasProgressRunning=false, progressStarted=false;
    uint64_t chordToken=0, progressToken=0;
    int progressIndex=0, musicArpPitch=-1, musicArpChannel=0;
    double progressRemaining=0, musicArpRemaining=0, musicGateRemaining=0, musicBeat=0;
    uint64_t musicArpToken=0;
    float musicArpVelocity=0;
    double nodeBPM(int route) const {
        const bool clock=routes[route-RouteArpIn]==ClockNode;
        return clock ? ((get(ClockSync)>.5f && hostTempo>0)?hostTempo:get(ClockBPM))
                     : (hostTempo>0?hostTempo:get(Tempo));
    }
    void makeChord(NoteSet& out,int root,int quality,uint64_t token,int velocity,int channel=0) {
        for(int degree:chordIntervals[std::clamp(quality,0,7)]) {
            int note=root+degree;
            if(degree>=0 && note>=0 && note<128) out[note*16+channel]={static_cast<uint8_t>(velocity),token};
        }
    }
    void musicUpdate() {
        musicNotes[MidiNode]=rootNotes;
        for(int node : {ChordNode,ProgressNode,ArpNode,VelocityNode}) musicNotes[node]={};
        bool chordOn=get(ChordRun)>.5f, progressOn=get(ProgressRun)>.5f;
        if(chordOn&&!wasChordRunning) chordToken=++serial;
        if(progressOn&&!wasProgressRunning) {progressIndex=0;progressRemaining=0;progressToken=++serial;progressStarted=false;}
        wasChordRunning=chordOn;wasProgressRunning=progressOn;
        if(progressOn) makeChord(musicNotes[ProgressNode],(int)get(ProgressRoot1+progressIndex*2),
                                (int)get(ProgressQuality1+progressIndex*2),progressToken,100);
        if(musicArpPitch>=0) musicNotes[ArpNode][musicArpPitch*16+musicArpChannel]={
            static_cast<uint8_t>(musicArpVelocity),musicArpToken};
        unsigned ready=(1u<<MidiNode)|(1u<<ProgressNode);
        // ARP is an event producer while enabled, a transparent MIDI node when bypassed.
        if(get(ArpOn)>.5f) ready|=1u<<ArpNode;
        for(int pass=0;pass<3;++pass) {
            for(int node : {ChordNode,VelocityNode,ArpNode}) {
                if(ready&(1u<<node))continue;
                int input=node==ChordNode?RouteChordIn:node==VelocityNode?RouteVelocityIn:RouteArpIn;
                int source=routes[input-RouteArpIn];
                if(source && !(ready&(1u<<source)))continue;
                auto& out=musicNotes[node];
                if(node==ChordNode) {
                    if(source) { for(int k=0;k<2048;++k) if(musicNotes[source][k].velocity)
                        makeChord(out,k/16,(int)get(ChordQuality),musicNotes[source][k].token,
                                  musicNotes[source][k].velocity,k%16); }
                    if(chordOn) makeChord(out,(int)get(ChordRoot),(int)get(ChordQuality),chordToken,100);
                } else if(source) {
                    out=musicNotes[source];
                    if(node==VelocityNode) {
                        double period=get(VelocityRate);
                        double phase=std::fmod(musicBeat/period,1.);
                        int shape=(int)get(VelocityShape);
                        double modulation=shape==0?.5-.5*std::cos(phase*2*pi):shape==1?phase:(phase<.5?1.:0.);
                        float lo=std::min(get(VelocityMin),get(VelocityMax)),hi=std::max(get(VelocityMin),get(VelocityMax));
                        for(auto& key:out) if(key.velocity) key.velocity=(uint8_t)std::round(lo+(hi-lo)*modulation);
                    }
                }
                ready|=1u<<node;
            }
        }
        for(int osc=0;osc<3;++osc) {
            int source=routes[RouteOsc1In+osc-RouteArpIn];
            for(int k=0;k<2048;++k) {
                MusicNote desired=source?musicNotes[source][k]:MusicNote{};
                auto& old=soundingNotes[osc][k];
                if(old.token==desired.token && bool(old.velocity)==bool(desired.velocity))continue;
                if(old.velocity) for(auto& voice:voices)
                    if(voice.channel==k%16 && voice.note==k/16)releaseVoice(voice,1u<<osc);
                if(desired.velocity)beginVoice(k%16,k/16,desired.velocity/127.f,1u<<osc,false);
                old=desired;
            }
        }
    }
    void musicTick() {
        musicBeat+=nodeBPM(RouteVelocityClock)/(60.*sr);
        bool changed=false;
        if(get(ProgressRun)>.5f && --progressRemaining<=0) {
            if(progressStarted)progressIndex=(progressIndex+1)%int(get(ProgressLength));
            progressStarted=true;progressToken=++serial;
            progressRemaining+=60.*sr/nodeBPM(RouteProgressClock)*4*get(ProgressBars);
            changed=true;
        }
        if(musicArpPitch>=0 && --musicGateRemaining<=0) {musicArpPitch=-1;changed=true;}
        if(get(ArpOn)>.5f && --musicArpRemaining<=0) {
            const double step=15.*sr/nodeBPM(RouteArpClock);
            musicArpRemaining+=step;
            if((patternMasks[(int)get(Pattern)]>>arpStep)&1u) {
                const auto& pool=musicNotes[routes[RouteArpIn-RouteArpIn]];
                const bool repeat=hitsOnPitch>0 && hitsOnPitch<int(get(ArpRepeat));
                for(int i=repeat?0:1;i<(repeat?2048:2049);++i) {
                    int k=(lastPitchKey+i)&2047;
                    if(!pool[k].velocity)continue;
                    hitsOnPitch=repeat&&k==lastPitchKey?hitsOnPitch+1:1;
                    lastPitchKey=k;musicArpPitch=k/16;musicArpChannel=k%16;
                    musicArpToken=++serial;
                    musicArpVelocity=pool[k].velocity*(arpStep%4==0?1.f:.85f);
                    int steps=1;while(steps<2 && !((patternMasks[(int)get(Pattern)]>>((arpStep+steps)&15))&1u))++steps;
                    musicGateRemaining=step*steps*get(Gate);changed=true;break;
                }
            }
            arpStep=(arpStep+1)&15;
        }
        if(changed)musicUpdate();
    }

    void beginVoice(int ch, int note, float velocity, unsigned mask, bool fromArp) {
        if (mask == 0) return;
        Voice* target = nullptr;
        for (auto& v : voices) {
            if (!v.active) { target = &v; break; }
            // Prefer the quietest voice already in release, then the oldest held voice.
            if (!target || (v.releasing && !target->releasing) ||
                (v.releasing && target->releasing && v.peakEnvelope() < target->peakEnvelope()) ||
                (!v.releasing && !target->releasing && v.age < target->age)) target = &v;
        }
        *target = {};
        target->active = true;
        target->oscillators = mask; target->fromArp = fromArp;
        for (int i=0; i<3; ++i) if (mask & (1u<<i)) target->envelopes[i].stage = attack;
        target->channel = ch; target->note = note;
        target->gain = velocity; target->hz = frequency[note]; target->age = ++serial;
    }

    static void releaseVoice(Voice& v, unsigned mask = 7) {
        if (!v.active || v.releasing) return;
        for (int i=0; i<3; ++i) {
            if (!(mask & (1u<<i))) continue;
            auto& env = v.envelopes[i];
            if (env.stage == idle || env.stage == release) continue;
            env.releaseFrom = env.level;
            env.stage = release;
            env.progress = 0;
        }
        if ((mask & v.oscillators) == v.oscillators) v.releasing = true;
    }

    void releaseNote(int ch, int note, bool fromArp) {
        for (auto& v : voices) {
            if (v.fromArp == fromArp && v.channel == ch && v.note == note) releaseVoice(v);
        }
    }
    void releaseArp() {
        if (arpChannel >= 0) releaseNote(arpChannel,arpPitch,true);
        arpChannel = arpPitch = -1;
    }
    void restartArp() {
        releaseArp(); untilStep = untilRelease = 0; arpStep = 0; lastPitchKey = -1; hitsOnPitch = 0;
    }
    void syncMode() {
        const float enable = get(ArpOn), pattern = get(Pattern);
        const uint64_t current = routeBits.load(std::memory_order_relaxed);
        if (current != appliedBits) {
            routes = unpackRouting(current);
            processingOrder(routes,order);
            appliedBits = current;
        }
        bool advanced=false;
        for(int from:routes) if(from>=ChordNode) advanced=true;
        if(advanced!=musicMode) {
            for(auto& voice:voices)releaseVoice(voice);
            soundingNotes={}; musicArpPitch=-1;musicArpRemaining=0;
            musicMode=advanced;
            if(!advanced){directMask=arpMask=0;heldCount=0;for(auto& channel:keys)for(auto& key:channel)if(key.velocity)++heldCount;}
        }
        if(musicMode) {directMask=arpMask=0;musicUpdate();return;}
        unsigned direct = 0, arpeggiated = 0;
        for (int i=0; i<3; ++i) {
            const int from = routes[RouteOsc1In+i-RouteArpIn];
            if (from == MidiNode) direct |= 1u<<i;
            else if (from == ArpNode && routes[0] == MidiNode) {
                if (enable) arpeggiated |= 1u<<i;
                else direct |= 1u<<i;
            }
        }
        if (direct != directMask || arpeggiated != arpMask) {
            for (auto& v : voices) {
                const unsigned removed = v.fromArp ? (arpMask & ~arpeggiated) : (directMask & ~direct);
                if (removed) releaseVoice(v,removed);
            }
            const unsigned gained = direct & ~directMask;
            if (gained && heldCount) {
                for (int ch=0; ch<16; ++ch) for (int note=0; note<128; ++note)
                    if (auto vel=keys[ch][note].velocity) beginVoice(ch,note,vel/127.f,gained,false);
            }
            if (!arpeggiated || !arpMask) restartArp();
            else if (arpChannel >= 0 && (arpeggiated & ~arpMask))
                beginVoice(arpChannel,arpPitch,keys[arpChannel][arpPitch].velocity/127.f,arpeggiated & ~arpMask,true);
            directMask = direct; arpMask = arpeggiated;
        }
        if (pattern != p[Pattern] && enable) restartArp();
        p[ArpOn] = enable; p[Pattern] = pattern;
    }
    void releaseKey(int ch, int note) {
        auto& key = keys[ch][note];
        if (!key.velocity) return;
        key = {}; --heldCount;
        releaseNote(ch,note,false);
        if (arpChannel == ch && arpPitch == note) releaseArp();
        if (lastPitchKey == note*16+ch) hitsOnPitch = 0;
        if (heldCount == 0) restartArp();
    }

    void playArpStep(double sixteenth) {
        const unsigned mask = patternMasks[static_cast<int>(p[Pattern])];
        if ((mask >> arpStep) & 1u) {
            releaseArp();
            const bool repeatPitch = hitsOnPitch > 0 && hitsOnPitch < static_cast<int>(p[ArpRepeat]);
            const int first = repeatPitch ? 0 : 1;
            for (int i=first; i<first+2048; ++i) {
                const int key = (lastPitchKey+i) & 2047;
                const int note = key/16, ch = key%16;
                const int velocity = keys[ch][note].velocity;
                if (!velocity) continue;
                hitsOnPitch = repeatPitch && key == lastPitchKey ? hitsOnPitch+1 : 1;
                lastPitchKey = key;
                const float accent = (arpStep%4 == 0 || int(p[Pattern]) == 2) ? 1.f : .85f;
                beginVoice(ch,note,velocity*accent/127.f,arpMask,true);
                arpChannel = ch; arpPitch = note;
                int steps = 1;
                while (steps < 2 && !((mask >> ((arpStep+steps)&15)) & 1u)) ++steps;
                untilRelease = sixteenth * steps * p[Gate];
                break;
            }
        }
        arpStep = (arpStep+1) & 15;
    }

    static float blep(double phase, double increment) {
        if (phase < increment) {
            const double t = phase/increment;
            return static_cast<float>(2*t-t*t-1);
        }
        if (phase > 1-increment) {
            const double t = (phase-1)/increment;
            return static_cast<float>(t*t+2*t+1);
        }
        return 0;
    }
    float wave(int shape, double phase, double increment) const {
        if (shape == 0) {
            const double pos = phase*tableSize;
            const int i = static_cast<int>(pos);
            return sine[i] + static_cast<float>(pos-i)*(sine[i+1]-sine[i]);
        }
        if (shape == 1) {
            const double shifted = phase < .5 ? phase+.5 : phase-.5;
            return (phase < .5 ? 1.f : -1.f) + blep(phase,increment) - blep(shifted,increment);
        }
        if (shape == 2) return static_cast<float>(2*phase-1) - blep(phase,increment);
        return static_cast<float>(1-4*std::abs(phase-.5));
    }
    static float advanceEnvelope(Envelope& env, double attackRate, double decayRate,
                                 float sustainLevel, double releaseRate) {
        switch (env.stage) {
            case attack:
                env.progress += attackRate;
                env.level = static_cast<float>(std::min(1.,env.progress));
                if (env.progress >= 1.) { env.stage = decay; env.progress = 0; }
                break;
            case decay:
                env.progress += decayRate;
                env.level = 1.f+static_cast<float>(std::min(1.,env.progress))*(sustainLevel-1.f);
                if (env.progress >= 1.) env.stage = sustain;
                break;
            case sustain: env.level = sustainLevel; break;
            case release:
                env.progress += releaseRate;
                env.level = env.releaseFrom*static_cast<float>(std::max(0.,1.-env.progress));
                if (env.progress >= 1.) env.stage = idle;
                break;
            case idle: break;
        }
        return env.level;
    }

    static float limit(float x) {
        const float mag = std::abs(x);
        if (mag <= .8f) return x;
        const float excess = mag-.8f;
        return std::copysign(.8f+excess/(1.f+5.f*excess),x);
    }
public:
    Engine() {
        for (int id=0; id<ParamCount; ++id) values[id].store(params[id].def,std::memory_order_relaxed);
        for (int i=0; i<=tableSize; ++i) sine[i] = static_cast<float>(std::sin(2*pi*i/tableSize));
        for (int i=0; i<128; ++i) frequency[i] = 440.*std::exp2((i-69)/12.);
        prepare(48000);
    }
    // Call while stopped. Parameter targets survive changes of sample rate.
    void prepare(double sampleRate) {
        sr = std::isfinite(sampleRate) ? std::clamp(sampleRate,8000.,384000.) : 48000.;
        for (auto& channel : echo) channel.resize(static_cast<size_t>(sr)+4);
        reset();
    }
    void reset() {
        voices = {}; keys = {}; pedal = {}; bend.fill(1.);
        musicNotes={};rootNotes={};soundingNotes={};musicMode=false;
        wasChordRunning=wasProgressRunning=false;
        musicBeat=progressRemaining=musicArpRemaining=musicGateRemaining=0;
        musicArpPitch=-1; progressIndex=0;
        serial = 0; heldCount = 0; arpChannel = arpPitch = -1; restartArp();
        for (auto& channel : echo) std::fill(channel.begin(),channel.end(),0.f);
        writeIndex = 0; filters = {};
        for (int i=0; i<ParamCount; ++i) p[i] = get(i);
        for (int i=0; i<3; ++i) {
            filters[i].configure(i,p[EQLow+i],sr);
            transpose[i] = std::exp2(p[Tune1+3*i]/12.);
        }
        delayCurrent = p[DelayTime]*sr*.001;
    }
    Routing routing() const { return unpackRouting(routeBits.load(std::memory_order_relaxed)); }
    bool setRouting(const Routing& proposed) {
        if (!validRouting(proposed)) return false;
        routeBits.store(packRouting(proposed),std::memory_order_relaxed);
        return true;
    }
    void set(int id, float value) {
        if (id<0 || id>=ParamCount || !std::isfinite(value)) return;
        if (routeParam(id)) {
            if (value<0 || value>static_cast<float>(VelocityNode)) return;
            const int source = static_cast<int>(std::round(value));
            uint64_t bits = routeBits.load(std::memory_order_relaxed);
            do {
                auto next = unpackRouting(bits);
                if (next[id-RouteArpIn] == source || !canConnect(next,id,source)) return;
                next[id-RouteArpIn] = source;
                if (routeBits.compare_exchange_weak(bits,packRouting(next),std::memory_order_relaxed)) return;
            } while (true);
        }
        value = std::clamp(value, params[id].min, params[id].max);
        if(id>=ChordRoot && id!=VelocityRate) value=std::round(value);
        if (id >= Osc1Attack && id <= Osc3Release && value < 0) value = Follow;
        if (id == Wave1 || id == Wave2 || id == Wave3 || id == Tune1 || id == Tune2 ||
            id == Tune3 || id == Pattern || id == ArpOn || id == ArpRepeat) value = std::round(value);
        values[id].store(value,std::memory_order_relaxed);
    }
    float get(int id) const {
        if (routeParam(id)) {
            return static_cast<float>((routeBits.load(std::memory_order_relaxed) >> (4*(id-RouteArpIn))) & 15u);
        }
        return id>=0 && id<ParamCount ? values[id].load(std::memory_order_relaxed) : 0.f;
    }
    void setHostTempo(double bpm) {
        hostTempo = std::isfinite(bpm) && bpm>=10 && bpm<=1000 ? bpm : 0.;
    }
    void midi(uint8_t status, uint8_t data1, uint8_t data2) {
        const int type = status&0xf0, ch = status&15, note = data1&127, velocity = data2&127;
        if (type != 0x80 && type != 0x90 && type != 0xb0 && type != 0xe0) return;
        syncMode();
        int index=note*16+ch;
        if(type==0x90 && velocity) rootNotes[index]={static_cast<uint8_t>(velocity),++serial};
        else if((type==0x80 || type==0x90) && !pedal[ch]) rootNotes[index]={};
        else if(type==0xb0 && (note==120 || note==123)) {
            for(int n=0;n<128;++n)rootNotes[n*16+ch]={};
            if(note==120) {set(ChordRun,0);set(ProgressRun,0);musicArpPitch=-1;for(auto& voice:voices)voice={};}
        }
        if(musicMode && type!=0xe0) {
            auto& key=keys[ch][note];
            if(type==0x90 && velocity) {key.down=std::min(255,int(key.down)+1);key.velocity=velocity;}
            else if(type==0x80 || type==0x90) {
                if(key.down)--key.down;
                if(!key.down && !pedal[ch])key={};
            } else if(type==0xb0 && note==64) {
                pedal[ch]=velocity>=64;
                if(!pedal[ch])for(int n=0;n<128;++n)if(!keys[ch][n].down){keys[ch][n]={};rootNotes[n*16+ch]={};}
            }
            musicUpdate();return;
        }
        auto& key = keys[ch][note];
        if (type == 0x90 && velocity) {
            if (key.velocity == 0) ++heldCount;
            if (key.down < 255) ++key.down;
            key.velocity = static_cast<unsigned char>(velocity);
            releaseNote(ch,note,false);
            beginVoice(ch,note,velocity/127.f,directMask,false);
        } else if (type == 0x80 || type == 0x90) {
            if (key.down) --key.down;
            if (!key.down && !pedal[ch]) releaseKey(ch,note);
        } else if (type == 0xe0) {
            const double semitones = ((velocity*128+note)-8192)*(2./8192.);
            bend[ch] = std::exp2(semitones/12.);
        } else if (note == 64 || note == 121) {
            pedal[ch] = note == 64 && velocity >= 64;
            if (note == 121) bend[ch] = 1;
            if (!pedal[ch]) for (int n=0; n<128; ++n) if (!keys[ch][n].down) releaseKey(ch,n);
        } else if (note == 120 || note == 123) {
            pedal[ch] = false;
            for (int n=0; n<128; ++n) releaseKey(ch,n);
            if (note == 120) for (auto& v : voices) if (v.channel == ch) v = {};
        }
    }
    // The buffers are planar. A single input channel is copied to both channels.
    // In-place rendering is supported. No memory allocation takes place here.
    void render(const float* inL, const float* inR, float* outL, float* outR, int frames) {
        if (frames<=0 || !outL || !outR) return;
        syncMode();
        for (int i=0; i<ParamCount; ++i) {
            if (routeParam(i)) continue;
            if (i == ArpOn || i == Pattern) continue;
            const float value = get(i);
            if (value != p[i]) {
                if (i >= EQLow && i <= EQHigh) filters[i-EQLow].configure(i-EQLow,value,sr);
                if (i == Tune1 || i == Tune2 || i == Tune3) transpose[(i-Tune1)/3] = std::exp2(value/12.);
                p[i] = value;
            }
        }
        const double decayRate = 1./(sr*p[Decay]);
        std::array<double,3> attackRate, releaseRate;
        std::array<float,3> sustainLevel;
        const auto effective = [&](int local, int shared) { return p[local] < 0 ? p[shared] : p[local]; };
        for (int osc=0; osc<3; ++osc) {
            const double a = effective(Osc1Attack+3*osc,Attack);
            const double r = effective(Osc1Release+3*osc,Release);
            attackRate[osc] = 1./std::max(1.,sr*a);
            releaseRate[osc] = 1./std::max(1.,sr*r);
            sustainLevel[osc] = effective(Osc1Sustain+3*osc,Sustain);
        }
        const double sixteenth = 15.*sr/(hostTempo ? hostTempo : p[Tempo]);
        const double delayTarget = p[DelayTime]*sr*.001;
        const double delaySlew = 1./(.025*sr);
        const int ringSize = static_cast<int>(echo[0].size());
        for (int n=0; n<frames; ++n) {
            if(musicMode) musicTick();
            if (!musicMode && arpMask && heldCount > 0) {
                if (arpChannel >= 0 && untilRelease <= 0) releaseArp();
                if (untilStep <= 0) { playArpStep(sixteenth); untilStep += sixteenth; }
                --untilStep; --untilRelease;
            }
            std::array<float,3> oscillators{};
            for (auto& v : voices) {
                if (!v.active) continue;
                const double fundamental = v.hz*bend[v.channel]/sr;
                const float gain = v.gain*.25f;
                bool active = false;
                for (int osc=0; osc<3; ++osc) {
                    auto& env = v.envelopes[osc];
                    if (env.stage == idle) continue;
                    const float level = advanceEnvelope(env,attackRate[osc],decayRate,sustainLevel[osc],releaseRate[osc]);
                    active |= env.stage != idle;
                    const double inc = fundamental*transpose[osc];
                    if (inc >= .49) continue;
                    const int waveID = Wave1 + 3*osc;
                    oscillators[osc] += gain*level*p[waveID+1]*wave(static_cast<int>(p[waveID]),v.phase[osc],inc);
                    v.phase[osc] += inc;
                    if (v.phase[osc] >= 1.) v.phase[osc] -= 1.;
                }
                v.active = active;
            }

            const float left = inL && std::isfinite(inL[n]) ? inL[n] : 0.f;
            const float right = inR ? (std::isfinite(inR[n]) ? inR[n] : 0.f) : left;
            const float input[2] = {left,right};
            delayCurrent += (delayTarget-delayCurrent)*delaySlew;
            const int delay = static_cast<int>(delayCurrent);
            const float fraction = static_cast<float>(delayCurrent-delay);
            int read = writeIndex-delay;
            if (read<0) read += ringSize;
            const int previous = read ? read-1 : ringSize-1;
            for (int ch=0; ch<2; ++ch) {
                std::array<float,DelayNode+1> bus{};
                for (int i=0; i<3; ++i) bus[Osc1Node+i] = oscillators[i];
                bus[InputNode] = input[ch]*p[InputGain];
                for (Node node : order) {
                    if (node == MixNode) {
                        for (int slot=RouteMix1; slot<=RouteMix4; ++slot)
                            bus[MixNode] += bus[routes[slot-RouteArpIn]];
                    } else if (node == EQNode) {
                        float x = bus[routes[RouteEQIn-RouteArpIn]];
                        for (auto& filter : filters) x = filter.tick(x,ch);
                        bus[EQNode] = x;
                    } else {
                        const float dry = bus[routes[RouteDelayIn-RouteArpIn]];
                        const float wet = echo[ch][read] + fraction*(echo[ch][previous]-echo[ch][read]);
                        const float store = dry + wet*p[DelayFeedback];
                        echo[ch][writeIndex] = std::abs(store)<1e-22f ? 0.f : store;
                        bus[DelayNode] = dry*(1.f-p[DelayMix])+wet*p[DelayMix];
                    }
                }
                (ch == 0 ? outL : outR)[n] = limit(bus[routes[RouteOutputIn-RouteArpIn]]*p[Master]);
            }
            if (++writeIndex == ringSize) writeIndex = 0;
        }
    }
};

} // namespace tri
