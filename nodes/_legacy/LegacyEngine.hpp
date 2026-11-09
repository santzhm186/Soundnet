#pragma once
#include "../../src/Harmony.hpp"
#include "../../platform/ParamInfo.hpp"
#include "../../src/MidiClip.hpp"
#include "../../src/NoteModel.hpp"
#include "../../src/RealtimeAsset.hpp"

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
    ProgressRoot7, ProgressQuality7, ProgressRoot8, ProgressQuality8,
    RouteClipClock, RouteModelIn, ClipRun, ClipLoop, MidiLocalOnly, RouteMidiOutIn, TriggerPulse, WaitSeconds, TriggerCount, ParamCount
};

enum Node {
    NoNode, MidiNode, ArpNode, Osc1Node, Osc2Node, Osc3Node,
    InputNode, MixNode, EQNode, DelayNode, OutputNode, ChordNode, ProgressNode, ClockNode, VelocityNode,
    DawMidiNode, ClipNode, ModelNode, MidiOutNode
};
inline constexpr const char* nodeNames[] = {
    "Disconnected", "MIDI", "ARP", "OSC 1", "OSC 2", "OSC 3",
    "Audio Input", "Mixer", "EQ", "Delay", "Output", "Chord", "Progression", "Clock", "Velocity",
    "DAW MIDI", "MIDI Clip", "Note Model", "MIDI Output"
};
inline constexpr int legacyRouteCount=RouteVelocityClock-RouteArpIn+1;
inline constexpr int routeIndex(int id) {
    if (id>=RouteArpIn && id<=RouteVelocityClock) return id-RouteArpIn;
    if (id==RouteClipClock) return legacyRouteCount;
    if (id==RouteModelIn) return legacyRouteCount+1;
    return id==RouteMidiOutIn ? legacyRouteCount+2 : -1;
}
inline constexpr int routeID(int index) {
    if (index<legacyRouteCount) return RouteArpIn+index;
    if (index==legacyRouteCount) return RouteClipClock;
    return index==legacyRouteCount+1 ? RouteModelIn : RouteMidiOutIn;
}
using Routing = std::array<int,legacyRouteCount+3>;
inline constexpr Routing defaultRouting = {
    MidiNode, ArpNode, ArpNode, ArpNode, Osc1Node, Osc2Node,
    Osc3Node, InputNode, MixNode, EQNode, DelayNode, NoNode, NoNode, NoNode, NoNode, NoNode
};
inline constexpr Node routeTarget(int param) {
    constexpr Node targets[] = {ArpNode,Osc1Node,Osc2Node,Osc3Node,MixNode,MixNode,
                                MixNode,MixNode,EQNode,DelayNode,OutputNode,ChordNode,VelocityNode,ArpNode,ProgressNode,VelocityNode,ClipNode,ModelNode,MidiOutNode};
    const int slot=routeIndex(param);
    return slot>=0 ? targets[slot] : NoNode;
}
inline constexpr std::array<int,9> noteSources = {NoNode,MidiNode,ArpNode,ChordNode,ProgressNode,VelocityNode,DawMidiNode,ClipNode,ModelNode};
inline bool noteSource(int n) { return n!=NoNode && std::find(noteSources.begin(),noteSources.end(),n)!=noteSources.end(); }
inline constexpr bool clockPort(int p) { return (p>=RouteArpClock && p<=RouteVelocityClock) || p==RouteClipClock; }
inline constexpr bool routeParam(int p) {return routeIndex(p)>=0;}
inline bool compatibleSource(int param,int source) {
    if(!routeParam(param))return false;
    if(!source)return true;
    if(clockPort(param))return source==ClockNode;
    if(param<=RouteOsc3In || param==RouteChordIn || param==RouteVelocityIn || param==RouteModelIn || param==RouteMidiOutIn)
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
    for (size_t i=0; i<r.size(); ++i)
        if (!compatibleSource(routeID(i),r[i])) return false;
    std::array<Node,3> order;
    if(!processingOrder(r,order))return false;
    for(int origin : {ArpNode,ChordNode,VelocityNode,ModelNode}) {
        int node=origin;
        for(int depth=0;depth<5;++depth) {
            int input=node==ArpNode?RouteArpIn:node==ChordNode?RouteChordIn:node==VelocityNode?RouteVelocityIn:node==ModelNode?RouteModelIn:-1;
            if(input<0)break;
            node=r[routeIndex(input)];
            if(node==origin)return false;
        }
    }
    return true;
}
inline bool canConnect(const Routing& r, int param, int source) {
    if (!compatibleSource(param,source)) return false;
    auto proposed = r;
    proposed[routeIndex(param)] = source;
    return validRouting(proposed);
}

// Port types have distinct source alphabets. The complete graph fits in a single
// lock-free word, so a render callback never sees a partially committed rewire.
inline constexpr int routeWidth(int id) {
    return clockPort(id) ? 1 : (id>=RouteMix1 && id<=RouteOutputIn) ? 3 : 4;
}
inline constexpr int encodeSource(int id,int source) {
    if (!source) return 0;
    if (clockPort(id)) return 1;
    if (routeWidth(id)==3) return source-Osc1Node+1;
    for (size_t i=1;i<noteSources.size();++i) if (noteSources[i]==source) return int(i);
    return 0;
}
inline constexpr uint64_t packRouting(const Routing& r) {
    uint64_t bits=0;
    int offset=0;
    for (size_t i=0;i<r.size();++i) {
        const int id=routeID(i);
        bits |= uint64_t(encodeSource(id,r[i]))<<offset;
        offset+=routeWidth(id);
    }
    return bits;
}
inline constexpr Routing unpackRouting(uint64_t bits) {
    Routing r{};
    for (size_t i=0;i<r.size();++i) {
        const int id=routeID(i), width=routeWidth(id), code=bits&((1u<<width)-1);
        if (code) r[i]=clockPort(id) ? ClockNode : width==3 ? Osc1Node+code-1 : noteSources[code];
        bits >>= width;
    }
    return r;
}
inline constexpr int routingBitCount() {
    int bits=0;
    for (size_t i=0;i<defaultRouting.size();++i) bits+=routeWidth(routeID(i));
    return bits;
}
static_assert(routingBitCount()<=64,"Audio thread graph must fit in one atomic word");

// Older sessions only serialize parameters through Master or ArpRepeat.
inline constexpr float Follow = -1.f;



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
    {"ARP Note Source",0,ModelNode,MidiNode,""}, {"Osc 1 Note Source",0,ModelNode,ArpNode,""},
    {"Osc 2 Note Source",0,ModelNode,ArpNode,""}, {"Osc 3 Note Source",0,ModelNode,ArpNode,""},
    {"Mix Input 1",0,9,Osc1Node,""}, {"Mix Input 2",0,9,Osc2Node,""},
    {"Mix Input 3",0,9,Osc3Node,""}, {"Mix Input 4",0,9,InputNode,""},
    {"EQ Source",0,9,MixNode,""}, {"Delay Source",0,9,EQNode,""}, {"Output Source",0,9,DelayNode,""},
    {"Chord Note Source",0,ModelNode,0,""}, {"Velocity Note Source",0,ModelNode,0,""},
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
    {"Progress Root 8",24,96,60,""}, {"Progress Quality 8",0,7,0,""},
    {"Clip Clock",0,ClockNode,0,""}, {"Model Note Source",0,ModelNode,0,""},
    {"Clip Play",0,1,0,""}, {"Clip Loop",0,1,1,""}, {"Local Keyboard Only",0,1,0,""},
    {"MIDI Output Note Source",0,ModelNode,0,""},
    {"Trigger Pulse",0,1000000,0,""}, {"Wait Seconds",0,3600,1,"s"}, {"Trigger Count",1,1000000,4,""}
};

class LegacyEngine {
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
        int channel = 0, note = 0, origin = 0;
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

    // origin 0 is legacy MIDI, 1 local hardware/keyboard, 2 DAW, 3 clip.
    struct MusicNote {
        uint64_t token;
        uint8_t velocity, origin;
        constexpr MusicNote(uint8_t v=0,uint64_t t=0,uint8_t port=0) : token(t),velocity(v),origin(port) {}
    };
    using NoteSet=std::array<MusicNote,2048>;
    struct LivePort {
        NoteSet notes{};
        std::array<uint8_t,2048> down{};
        std::array<bool,16> sustain{};
        std::array<double,16> bend{};
        void reset() { notes.fill({}); down.fill(0); sustain.fill(false); bend.fill(1.); }
        void apply(int type,int ch,int n,int v,uint64_t token,uint8_t origin) {
            const int key=n*16+ch;
            if (type==0x90 && v) {
                if (down[key]<255) ++down[key];
                notes[key]={uint8_t(v),token,origin};
            } else if (type==0x80 || type==0x90) {
                if (down[key]) --down[key];
                if (!down[key] && !sustain[ch]) notes[key]={};
            } else if (type==0xe0) {
                bend[ch]=std::exp2(((v*128+n)-8192)*(2./(8192.*12.)));
            } else if (type==0xb0 && (n==64 || n==121 || n==120 || n==123)) {
                sustain[ch]=n==64 && v>=64;
                if (n==121) bend[ch]=1.;
                if (!sustain[ch]) for (int i=ch;i<2048;i+=16) {
                    if (n==120 || n==123) down[i]=0;
                    if (!down[i]) notes[i]={};
                }
            }
        }
    };
    LivePort localPort,dawPort;
    std::array<NoteSet,ModelNode+1> musicNotes{};
    std::array<NoteSet,3> soundingNotes{};
    NoteSet sentNotes{};
    using MidiEmit=void (*)(void*,int,uint8_t,uint8_t,uint8_t);
    MidiEmit midiEmit=nullptr;
    void* midiContext=nullptr;
    int emitFrame=0;
    void flushMidiOutput() {
        for (int k=0;k<2048;++k) if (sentNotes[k].velocity) {
            if (midiEmit) midiEmit(midiContext,emitFrame,0x80|(k%16),k/16,0);
            sentNotes[k]={};
        }
    }
    void updateMidiOutput() {
        if (!midiEmit) return;
        const auto& desired=musicNotes[routes[routeIndex(RouteMidiOutIn)]];
        // Emit releases before attacks. In particular, distinct input notes
        // mapped to the same pitch must never leave a downstream voice stuck.
        for (int k=0;k<2048;++k) if (sentNotes[k].velocity && sentNotes[k].token!=desired[k].token) {
            midiEmit(midiContext,emitFrame,0x80|(k%16),k/16,0);
            sentNotes[k]={};
        }
        for (int k=0;k<2048;++k) if (desired[k].velocity && !sentNotes[k].velocity) {
            midiEmit(midiContext,emitFrame,0x90|(k%16),k/16,desired[k].velocity);
            sentNotes[k]=desired[k];
        }
    }
    RealtimeAsset<MidiClip> clipAsset;
    RealtimeAsset<NoteModel> modelAsset;
    std::array<unsigned,2048> clipDown{};
    double clipPosition=0;
    size_t clipCursor=0;
    bool clipActive=false, clipFinished=false, clipNeedsReset=true;
    bool sawDAW=false, sawLocal=false;
    bool musicMode=false, isolated=false, chordActive=false, progressActive=false, arpActive=false;
    bool transportValid=false, transportPlaying=false, transportJump=false;
    double hostBeat=0, musicBeat=0, progressRemaining=0, musicArpRemaining=0, musicGateRemaining=0;
    double clipIncrement=0, progressIncrement=0, arpIncrement=0, velocityIncrement=0;
    unsigned previousHostClockMask=0;
    uint64_t chordToken=0, progressToken=0, musicArpToken=0;
    int progressIndex=0, musicArpPitch=-1, musicArpChannel=0;
    uint8_t musicArpVelocity=0, musicArpOrigin=0;

    bool hostClock(int port) const {
        return routes[routeIndex(port)]!=ClockNode || get(ClockSync)>.5f;
    }
    double nodeBPM(int port) const {
        if (hostClock(port) && hostTempo>0) return hostTempo;
        return routes[routeIndex(port)]==ClockNode ? get(ClockBPM) : get(Tempo);
    }
    void clearClipNotes() {
        musicNotes[ClipNode].fill({});
        clipDown.fill(0);
        clipCursor=0;
    }
    void clipEvent(const ClipEvent& e) {
        const int k=e.data1*16+(e.status&15);
        if ((e.status&0xf0)==0x90 && e.data2) {
            ++clipDown[k];
            musicNotes[ClipNode][k]={e.data2,++serial,3};
        } else {
            if (clipDown[k]) --clipDown[k];
            if (!clipDown[k]) musicNotes[ClipNode][k]={};
        }
    }
    void* cycleContext=nullptr;
    void (*cycleEmit)(void*,int)=nullptr;
    bool tickClip() {
        if (!clipActive || clipFinished) return false;
        const auto& clip=clipAsset.current();
        bool changed=false;
        if (clipPosition+1e-9>=clip.lengthBeats) {
            if(cycleEmit)cycleEmit(cycleContext,emitFrame);
            clearClipNotes();
            if(get(ClipRun)<.5f){clipActive=false;clipFinished=true;return true;}
            changed=true;
            if (get(ClipLoop)<.5f) { clipFinished=true; return true; }
            clipPosition=std::fmod(clipPosition+1e-9,clip.lengthBeats);
        }
        while (clipCursor<clip.events.size() && clip.events[clipCursor].beat<=clipPosition+1e-9) {
            clipEvent(clip.events[clipCursor++]);
            changed=true;
        }
        clipPosition+=clipIncrement;
        return changed;
    }
    void syncMusicClocks() {
        const auto& clip=clipAsset.current();
        unsigned mask=0;
        for (int id : {RouteClipClock,RouteProgressClock,RouteArpClock,RouteVelocityClock})
            if (transportValid && hostClock(id)) mask|=1u<<(routeIndex(id)-routeIndex(RouteArpClock));
        auto follows=[&](int id) { return (mask & (1u<<(routeIndex(id)-routeIndex(RouteArpClock))))!=0; };
        auto restart=[&](int id) {
            const unsigned bit=1u<<(routeIndex(id)-routeIndex(RouteArpClock));
            return ((mask^previousHostClockMask)&bit)!=0 || (transportJump && follows(id));
        };
        auto enabled=[&](int id) { return !follows(id) || transportPlaying; };
        clipIncrement=nodeBPM(RouteClipClock)/(60.*sr);
        progressIncrement=nodeBPM(RouteProgressClock)/(60.*sr);
        arpIncrement=nodeBPM(RouteArpClock)/(60.*sr);
        velocityIncrement=enabled(RouteVelocityClock) ? nodeBPM(RouteVelocityClock)/(60.*sr) : 0;
        if (follows(RouteVelocityClock)) musicBeat=std::max(0.,hostBeat);
        const bool runClip=get(ClipRun)>.5f && !clip.events.empty() && clip.lengthBeats>0 && enabled(RouteClipClock);
        if (clipActive!=runClip || clipNeedsReset || restart(RouteClipClock)) {
            clearClipNotes();
            clipPosition=follows(RouteClipClock) ? hostBeat : 0.;
            clipFinished=false;
            if (runClip && clipPosition>=clip.lengthBeats && get(ClipLoop)>.5f)
                clipPosition=std::fmod(clipPosition,clip.lengthBeats);
            clipActive=runClip; clipNeedsReset=false;
        }
        const bool runProgress=get(ProgressRun)>.5f && enabled(RouteProgressClock);
        if (runProgress && (!progressActive || restart(RouteProgressClock))) {
            const double position=follows(RouteProgressClock) ? std::max(0.,hostBeat) : 0;
            const double bars=4.*get(ProgressBars);
            progressIndex=int(std::fmod(std::floor(position/bars),get(ProgressLength)));
            progressRemaining=bars-std::fmod(position,bars);
            progressToken=++serial;
        }
        progressActive=runProgress;
        const bool runArp=get(ArpOn)>.5f && enabled(RouteArpClock);
        if (arpActive!=runArp || restart(RouteArpClock)) {
            musicArpPitch=-1; lastPitchKey=-1; hitsOnPitch=0;
            const double position=follows(RouteArpClock) ? std::max(0.,hostBeat) : 0;
            const double next=std::ceil(position*4.-1e-8);
            arpStep=int(std::fmod(next,16.)); musicArpRemaining=std::max(0.,next*.25-position);
        }
        arpActive=runArp;
        const bool runChord=get(ChordRun)>.5f;
        if (runChord && !chordActive) chordToken=++serial;
        chordActive=runChord;
        previousHostClockMask=mask; transportJump=false;
    }
    void makeChord(NoteSet& out,int root,int quality,uint64_t token,int velocity,int channel=0,uint8_t origin=0) {
        for(int degree:chordIntervals[std::clamp(quality,0,7)]) {
            int note=root+degree;
            if(degree>=0 && note>=0 && note<128) out[note*16+channel]={static_cast<uint8_t>(velocity),token,origin};
        }
    }
    void musicUpdate() {
        musicNotes[MidiNode]=localPort.notes;
        musicNotes[DawMidiNode]=dawPort.notes;
        if (!isolated) for (size_t k=0;k<localPort.notes.size();++k) {
            if (dawPort.notes[k].token>localPort.notes[k].token)
                musicNotes[MidiNode][k]=dawPort.notes[k];
        }
        for (int node : {ChordNode,ProgressNode,ArpNode,VelocityNode,ModelNode}) musicNotes[node].fill({});
        if(progressActive) makeChord(musicNotes[ProgressNode],(int)get(ProgressRoot1+progressIndex*2),
                                (int)get(ProgressQuality1+progressIndex*2),progressToken,100);
        if(musicArpPitch>=0) musicNotes[ArpNode][musicArpPitch*16+musicArpChannel]={
            musicArpVelocity,musicArpToken,musicArpOrigin};
        unsigned ready=(1u<<MidiNode)|(1u<<DawMidiNode)|(1u<<ClipNode)|(1u<<ProgressNode);
        // ARP is an event producer while enabled, a transparent MIDI node when bypassed.
        if(get(ArpOn)>.5f) ready|=1u<<ArpNode;
        for(int pass=0;pass<4;++pass) {
            for(int node : {ChordNode,VelocityNode,ArpNode,ModelNode}) {
                if(ready&(1u<<node))continue;
                int input=node==ChordNode?RouteChordIn:node==VelocityNode?RouteVelocityIn:node==ModelNode?RouteModelIn:RouteArpIn;
                int source=routes[routeIndex(input)];
                if(source && !(ready&(1u<<source)))continue;
                auto& out=musicNotes[node];
                if(node==ChordNode) {
                    if(source) { for(int k=0;k<2048;++k) if(musicNotes[source][k].velocity)
                        makeChord(out,k/16,(int)get(ChordQuality),musicNotes[source][k].token,
                                  musicNotes[source][k].velocity,k%16,musicNotes[source][k].origin); }
                    if(chordActive) makeChord(out,(int)get(ChordRoot),(int)get(ChordQuality),chordToken,100);
                } else if (node==ModelNode && source) {
                    for (int k=0;k<2048;++k) {
                        const auto& inputNote=musicNotes[source][k];
                        if (!inputNote.velocity) continue;
                        auto& output=out[modelAsset.current().pitches[k/16]*16+k%16];
                        if (inputNote.token>output.token) output=inputNote;
                    }
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
        updateMidiOutput();
        for(int osc=0;osc<3;++osc) {
            int source=routes[RouteOsc1In+osc-RouteArpIn];
            for(int k=0;k<2048;++k) {
                MusicNote desired=source?musicNotes[source][k]:MusicNote{};
                auto& old=soundingNotes[osc][k];
                if(old.token==desired.token && bool(old.velocity)==bool(desired.velocity))continue;
                if(old.velocity) for(auto& voice:voices)
                    if(voice.channel==k%16 && voice.note==k/16)releaseVoice(voice,1u<<osc);
                if(desired.velocity)beginVoice(k%16,k/16,desired.velocity/127.f,1u<<osc,false,desired.origin);
                old=desired;
            }
        }
    }
    void musicTick() {
        musicBeat+=velocityIncrement;
        bool clockEvent=tickClip();
        if(progressActive && progressRemaining<=1e-9) {
            progressIndex=(progressIndex+1)%int(get(ProgressLength));
            progressToken=++serial;
            progressRemaining+=4*get(ProgressBars);
            clockEvent=true;
        }
        if (progressActive) progressRemaining-=progressIncrement;
        // Source events at this sample must reach the arpeggiator before its tick.
        if (clockEvent) musicUpdate();
        bool changed=false;
        if(musicArpPitch>=0 && (musicGateRemaining-=arpIncrement)<=0) {musicArpPitch=-1;changed=true;}
        if(arpActive && musicArpRemaining<=1e-9) {
            const double step=.25;
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
                    musicArpOrigin=pool[k].origin;musicArpVelocity=pool[k].velocity*(arpStep%4==0?1.f:.85f);
                    int steps=1;while(steps<2 && !((patternMasks[(int)get(Pattern)]>>((arpStep+steps)&15))&1u))++steps;
                    musicGateRemaining=step*steps*get(Gate);changed=true;break;
                }
            }
            arpStep=(arpStep+1)&15;
        }
        if (arpActive) musicArpRemaining-=arpIncrement;
        if(changed)musicUpdate();
    }

    void beginVoice(int ch, int note, float velocity, unsigned mask, bool fromArp, int origin=0) {
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
        target->origin=origin; target->channel = ch; target->note = note;
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
        if (clipAsset.consume()) clipNeedsReset=true;
        modelAsset.consume();
        isolated=get(MidiLocalOnly)>.5f || std::find(routes.begin(),routes.end(),DawMidiNode)!=routes.end();
        const bool advanced=isolated || routes[routeIndex(RouteMidiOutIn)] || (sawDAW && sawLocal) || (transportValid && enable) ||
            std::any_of(routes.begin(),routes.end(),[](int source){return source>=ChordNode;});
        if (advanced!=musicMode) {
            for(auto& voice:voices) releaseVoice(voice);
            for(auto& set:soundingNotes) set.fill({});
            musicArpPitch=-1; musicArpRemaining=0;
            arpActive=progressActive=chordActive=clipActive=false;
            clipNeedsReset=true;
            directMask=arpMask=0;
            if (!advanced) {
                flushMidiOutput();
                // Restore physically held keys when removing the last note processor.
                const auto& port=sawDAW ? dawPort : localPort;
                pedal=port.sustain; bend=port.bend; heldCount=0;
                for (int k=0;k<2048;++k) {
                    keys[k%16][k/16]={port.down[k],port.notes[k].velocity};
                    if (port.notes[k].velocity) ++heldCount;
                }
            }
            musicMode=advanced;
        }
        if (musicMode) {syncMusicClocks();musicUpdate();return;}
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
    LegacyEngine() {
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
        emitFrame=0; flushMidiOutput();
        voices = {}; keys = {}; pedal = {}; bend.fill(1.);
        for (auto& bank:musicNotes) bank.fill({});
        for (auto& bank:soundingNotes) bank.fill({});
        localPort.reset(); dawPort.reset(); sawDAW=sawLocal=false;
        clearClipNotes();clipActive=false;clipNeedsReset=true;
        chordActive=progressActive=arpActive=musicMode=false;
        transportValid=transportPlaying=transportJump=false; hostBeat=0; previousHostClockMask=0;
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
    void panic() {
        set(ClipRun,0);set(ChordRun,0);set(ProgressRun,0);
        reset();
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
            if (value<0 || value>static_cast<float>(ModelNode)) return;
            const int source = static_cast<int>(std::round(value));
            uint64_t bits = routeBits.load(std::memory_order_relaxed);
            do {
                auto next = unpackRouting(bits);
                if (next[routeIndex(id)] == source || !canConnect(next,id,source)) return;
                next[routeIndex(id)] = source;
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
            return static_cast<float>(routing()[routeIndex(id)]);
        }
        return id>=0 && id<ParamCount ? values[id].load(std::memory_order_relaxed) : 0.f;
    }
    void setHostTempo(double bpm) {
        hostTempo = std::isfinite(bpm) && bpm>=10 && bpm<=1000 ? bpm : 0.;
    }
    // Host callbacks invoke these on the render thread, once per unsplit buffer.
    void setHostTransport(bool playing,double beat) {
        if (!std::isfinite(beat)) { clearHostTransport(); return; }
        transportJump|=!transportValid || playing!=transportPlaying || std::abs(beat-hostBeat)>.002;
        hostBeat=beat; transportValid=true; transportPlaying=playing;
    }
    void clearHostTransport() { transportValid=false; transportPlaying=false; }
    // Install while stopped or from the render thread. Frames are relative to the
    // current render() span; MIDI delivered between spans has offset zero.
    void setMidiOutput(void* context,void (*emit)(void*,int,uint8_t,uint8_t,uint8_t)) {
        if (midiEmit==emit && midiContext==context) return;
        emitFrame=0; flushMidiOutput();
        midiContext=context; midiEmit=emit;
    }
    // Parsing, validation and publication happen off the render thread.
    void setCycleOutput(void* context,void (*emit)(void*,int)) {cycleContext=context;cycleEmit=emit;}
    bool setClip(const MidiClip& clip) {
        if (!std::isfinite(clip.lengthBeats) || clip.lengthBeats<0 || clip.lengthBeats>65536 ||
            clip.events.size()>32768 || (!clip.events.empty() && clip.lengthBeats<=0)) return false;
        double previous=0;
        for (const auto& event:clip.events) {
            const int type=event.status&0xf0;
            if (!std::isfinite(event.beat) || event.beat<previous || event.beat>clip.lengthBeats ||
                (type!=0x80 && type!=0x90) || event.data1>127 || event.data2>127) return false;
            previous=event.beat;
        }
        clipAsset.publish(clip); return true;
    }
    bool setModel(const NoteModel& model) {
        for (auto pitch:model.pitches) if (pitch>=128) return false;
        modelAsset.publish(model); return true;
    }
    void midi(uint8_t status,uint8_t data1,uint8_t data2) { liveMidi(false,status,data1,data2); }
    void dawMidi(uint8_t status,uint8_t data1,uint8_t data2) { liveMidi(true,status,data1,data2); }
private:
    void liveMidi(bool daw,uint8_t status,uint8_t data1,uint8_t data2) {
        const int type=status&0xf0, ch=status&15, note=data1&127, velocity=data2&127;
        if (type!=0x80 && type!=0x90 && type!=0xb0 && type!=0xe0) return;
        if (type==0x90 && velocity) (daw ? sawDAW : sawLocal)=true;
        syncMode();
        (daw ? dawPort : localPort).apply(type,ch,note,velocity,++serial,daw?2:1);
        if (!daw && type==0xb0 && note==120) dawPort.apply(type,ch,note,velocity,serial,2);
        if (musicMode) {
            if (type==0xb0 && note==120) {
                for (auto& voice:voices)
                    if (voice.channel==ch && voice.origin==(daw?2:1)) voice={};
                if (!daw) {
                    // The editor sends All Sound Off on all 16 local channels.
                    for (auto& voice:voices) if (voice.channel==ch) voice={};
                    set(ChordRun,0);set(ProgressRun,0);set(ClipRun,0);
                    chordActive=progressActive=clipActive=false; musicArpPitch=-1;
                    clearClipNotes();
                    for(auto& voice:voices) if (voice.origin==0 || voice.origin==3) voice={};
                }
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
public:
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
            emitFrame=n;
            if(musicMode) musicTick();
            if (!musicMode && arpMask && heldCount > 0) {
                if (arpChannel >= 0 && untilRelease <= 0) releaseArp();
                if (untilStep <= 0) { playArpStep(sixteenth); untilStep += sixteenth; }
                --untilStep; --untilRelease;
            }
            std::array<float,3> oscillators{};
            for (auto& v : voices) {
                if (!v.active) continue;
                const double pitchBend=v.origin==1 ? localPort.bend[v.channel] :
                    v.origin==2 ? dawPort.bend[v.channel] : v.origin==3 ? 1. : bend[v.channel];
                const double fundamental = v.hz*pitchBend/sr;
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
        emitFrame=0;
        if (transportValid && transportPlaying) hostBeat+=frames*(hostTempo>0?hostTempo:p[Tempo])/(60.*sr);
    }
};

} // namespace tri
