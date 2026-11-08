#pragma once
// NodeSpec — single source of truth for node identity, ports, parameters,
// execution capabilities and time semantics.
//
// Everything else derives from this header: the palette, the inspector, the
// connection rules, project persistence, the run plan and the offline renderer.
// Adding a node type means adding one entry here plus its kernel; it no longer
// means editing six files and twenty switch statements.
//
// Stability rules (see docs/节点与连接标准-实现映射.md):
//   * `nodeKey` is permanent. It is the wire identity of a node type.
//   * `type` is a runtime kernel selector. New nodes append new numbers; the
//     existing numbers never change and never act as a validity ceiling.
//   * Port IDs are permanent. Port order is the legacy v3 index order and may
//     only be appended to, never reordered.
//   * Parameter IDs are permanent; `ParamSpec::key` is their readable alias.
#include "LegacyEngine.hpp"
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace tri {

// ---------------------------------------------------------------------------
// Node type IDs
// ---------------------------------------------------------------------------
// Legacy engine nodes (see LegacyEngine.hpp for 1..18).
inline constexpr int HardwareOutputNode=19, DawOutputNode=20, DawClockNode=21, DawAudioNode=22;
inline constexpr int StartNode=23, StopNode=24, WaitNode=25, CountNode=26, ArrangerNode=27;
inline constexpr int FilterNode=28, ReverbNode=29, NoiseNode=30;
inline constexpr int SamplerNode=31;
inline constexpr int AudioClipNode=32, CompressorNode=33, LimiterNode=34, ChorusNode=35;
inline constexpr int QuantizeNode=36;
inline constexpr int TremoloNode=37, BitcrusherNode=38, PhaserNode=39;
inline constexpr int RandomNode=40;
inline constexpr int MetronomeNode=41;
inline constexpr int TransposeNode=42;
inline constexpr int EchoNode=43;
inline constexpr int GateNode=44;
inline constexpr int ChannelFilterNode=45;
inline constexpr int CommentNode=46;
// Appended after the v3 format shipped. Reserved gaps are allowed; what is
// forbidden is treating the last known number as a validity bound.
inline constexpr int FirstAppendedNode=47;

// ---------------------------------------------------------------------------
// Signals
// ---------------------------------------------------------------------------
// `Notes` is kept as an alias so existing call sites read naturally, but the
// contract name is `Midi`.
enum class Signal : uint8_t { None, Audio, Midi, Trigger, Clock, Control, Resource, TaskEvent, Notes=Midi };

inline const char* signalKey(Signal s) {
    switch(s) {
        case Signal::Audio: return "audio";
        case Signal::Midi: return "midi";
        case Signal::Trigger: return "trigger";
        case Signal::Clock: return "clock";
        case Signal::Control: return "control";
        case Signal::Resource: return "resource";
        case Signal::TaskEvent: return "taskEvent";
        default: return "none";
    }
}
inline bool signalFromKey(const char* key,Signal& out) {
    if(!key) return false;
    struct Row { const char* key; Signal signal; };
    static const Row rows[] = {
        {"audio",Signal::Audio},{"midi",Signal::Midi},{"notes",Signal::Midi},{"trigger",Signal::Trigger},
        {"clock",Signal::Clock},{"control",Signal::Control},{"resource",Signal::Resource},
        {"taskEvent",Signal::TaskEvent},{"none",Signal::None}
    };
    for(const auto& row:rows) if(std::strcmp(row.key,key)==0) {out=row.signal;return true;}
    return false;
}

// ---------------------------------------------------------------------------
// Ports
// ---------------------------------------------------------------------------
enum PortRole : uint8_t {
    RoleData=0,      // ordinary payload input/output
    RoleMain,        // the node's primary note/payload input (never positional)
    RoleClock,       // timing input or output
    RoleSidechain,   // detector input
    RoleTrigger,     // causal start/stop style input or output
    RoleControl,     // automation / transport style input
    RoleResource,    // immutable content handle
    RoleEvent,       // task lifecycle event
    RoleOutput       // terminal sink (audio leaves the graph here)
};
enum PortFlags : uint8_t {
    PortMultiSource  = 1<<0,  // several upstream edges may share this input
    PortOptional     = 1<<1,  // may stay unconnected
    PortFeedback     = 1<<2,  // temporal feedback boundary: exempt from DAG order
};
struct PortSpec {
    const char* id;      // stable, persisted in v4 projects
    const char* label;   // display only, may change freely
    Signal signal;
    PortRole role;
    uint8_t flags;
};

// ---------------------------------------------------------------------------
// Execution capabilities and time semantics
// ---------------------------------------------------------------------------
inline constexpr uint32_t CapData          =1u<<0;  // evaluate() from input snapshot
inline constexpr uint32_t CapGenerator     =1u<<1;  // produces output without inputs
inline constexpr uint32_t CapStream        =1u<<2;  // realtime/offline block processing
inline constexpr uint32_t CapTask          =1u<<3;  // background or external task
inline constexpr uint32_t CapMultiOutput   =1u<<4;  // holds state per output port
inline constexpr uint32_t CapNoteSource    =1u<<5;  // emits a MIDI/note payload
inline constexpr uint32_t CapClockSource   =1u<<6;  // provides a time domain position
inline constexpr uint32_t CapOutputSink    =1u<<7;  // summed into the graph output
inline constexpr uint32_t CapTriggerSource =1u<<8;  // emits causal trigger events
inline constexpr uint32_t CapTriggerTarget =1u<<9;  // consumes causal trigger events
inline constexpr uint32_t CapBypassable    =1u<<10;
inline constexpr uint32_t CapAssetClip     =1u<<11;
inline constexpr uint32_t CapAssetAudio    =1u<<12;
inline constexpr uint32_t CapAssetModel    =1u<<13;
inline constexpr uint32_t CapTemporal      =1u<<14; // needs a musical/clock time domain
inline constexpr uint32_t CapSnapshot      =1u<<15; // may publish an immutable result resource

// How a node relates to content time. Ordinary data nodes are TimeMode::None and
// must run without a sample rate, BPM or transport.
enum class TimeMode : uint8_t { None, Content, Timed, Stream };

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------
struct ParamSpec {
    const ParamInfo* info;
    const char* key;   // stable alias, used by the v4 project format
    bool discrete;     // integral value; validation rejects fractions
};

// Graph-node parameter IDs. These live above the legacy AU parameter range so a
// graph instance never collides with an AU preset slot.
inline constexpr int FilterType=1100, FilterCutoff=1101, FilterResonance=1102, FilterDrive=1103;
inline constexpr int ReverbMix=1110, ReverbSize=1111, ReverbDamping=1112, ReverbWidth=1113,
    ReverbPreDelay=1114;
inline constexpr int NoiseLevel=1120, NoiseType=1121;
inline constexpr int SamplerRoot=1130, SamplerGain=1131, SamplerLoop=1132;
inline constexpr int SamplerAttack=1133, SamplerRelease=1134;
inline constexpr int SamplerStart=1135, SamplerEnd=1136;
inline constexpr int SamplerReverse=1137, SamplerVelocity=1138, SamplerXfade=1139;
inline constexpr int AudioClipGain=1140, AudioClipLoop=1141;
inline constexpr int AudioClipStart=1142, AudioClipEnd=1143, AudioClipFade=1144;
inline constexpr int AudioClipPitch=1145, AudioClipReverse=1146, AudioClipXfade=1147;
inline constexpr int CompThreshold=1150, CompRatio=1151, CompAttack=1152;
inline constexpr int CompRelease=1153, CompMakeup=1154, CompMix=1155;
inline constexpr int LimiterThreshold=1160, LimiterRelease=1161, LimiterCeiling=1162, LimiterGain=1163;
inline constexpr int ChorusRate=1170, ChorusDepth=1171, ChorusMix=1172, ChorusDelay=1173,
    ChorusFeedback=1174;
inline constexpr int QuantizeRoot=1180, QuantizeScale=1181, QuantizeMode=1182;
inline constexpr int TremoloRate=1190, TremoloDepth=1191, TremoloMix=1192, TremoloStereo=1193,
    TremoloSync=1194, TremoloDivision=1195;
inline constexpr int CrushBits=1200, CrushRate=1201, CrushMix=1202;
inline constexpr int PhaserRate=1210, PhaserDepth=1211, PhaserFeedback=1212, PhaserMix=1213;
inline constexpr int RandomChance=1220;
inline constexpr int MetronomeBPM=1230, MetronomeLevel=1231, MetronomeAccent=1232, MetronomeBeats=1233;
inline constexpr int TransposeSemitones=1240;
inline constexpr int EchoTime=1250, EchoRepeats=1251, EchoFeedback=1252, EchoPitch=1253;
inline constexpr int GateThreshold=1260, GateAttack=1261, GateRelease=1262, GateRange=1263;
inline constexpr int MidiOutChannel=1270;
inline constexpr int OutputWidth=1280;
inline constexpr int DelaySync=1290, DelayDivision=1291;
inline constexpr int ChannelFilter=1300;
// Voice texture for the three-voice arranger.
enum class ArrangerTexture { Highest, Middle, Lowest, Chord };
// Arranger parameters are stored on the instance, outside the AU parameter list.
enum ArrangerParam {
    ArrHighMin=1024, ArrHighMax, ArrHighTexture,
    ArrMidMin, ArrMidMax, ArrMidTexture,
    ArrLowMin, ArrLowMax, ArrLowTexture
};

inline constexpr ParamInfo filterTypeInfo   {"Type",0,2,0,""};
inline constexpr ParamInfo filterCutoffInfo {"Cutoff",20,20000,8000,"Hz"};
inline constexpr ParamInfo filterResonanceInfo {"Resonance",.5f,20,.9f,"Q"};
inline constexpr ParamInfo filterDriveInfo  {"Drive",0,1,0,""};
inline constexpr ParamInfo reverbMixInfo    {"Mix",0,1,.25f,""};
inline constexpr ParamInfo reverbSizeInfo   {"Size",0,1,.55f,""};
inline constexpr ParamInfo reverbDampingInfo{"Damping",0,1,.4f,""};
inline constexpr ParamInfo reverbWidthInfo  {"Width",0,1,.9f,""};
inline constexpr ParamInfo reverbPreDelayInfo{"PreDelay",0,200,0,"ms"};
inline constexpr ParamInfo noiseLevelInfo   {"Level",0,1,.2f,""};
inline constexpr ParamInfo noiseTypeInfo    {"Type",0,1,0,""};
inline constexpr ParamInfo samplerRootInfo   {"Root",0,127,60,"MIDI"};
inline constexpr ParamInfo samplerGainInfo   {"Gain",0,1,.8f,""};
inline constexpr ParamInfo samplerLoopInfo   {"Loop",0,1,0,""};
inline constexpr ParamInfo samplerAttackInfo {"Attack",.001f,5,.005f,"s"};
inline constexpr ParamInfo samplerReleaseInfo{"Release",.001f,5,.12f,"s"};
inline constexpr ParamInfo samplerReverseInfo{"Reverse",0,1,0,""};
inline constexpr ParamInfo samplerVelocityInfo{"Velocity",0,1,1,""};
inline constexpr ParamInfo samplerXfadeInfo{"Loop XFade",0,100,5,"ms"};
inline constexpr ParamInfo samplerStartInfo  {"Start",0,1,0,""};
inline constexpr ParamInfo samplerEndInfo    {"End",0,1,1,""};
inline constexpr ParamInfo audioClipGainInfo  {"Gain",0,1,.9f,""};
inline constexpr ParamInfo audioClipLoopInfo  {"Loop",0,1,0,""};
inline constexpr ParamInfo audioClipStartInfo {"Start",0,1,0,""};
inline constexpr ParamInfo audioClipEndInfo   {"End",0,1,1,""};
inline constexpr ParamInfo audioClipFadeInfo  {"Fade",0,2,.01f,"s"};
inline constexpr ParamInfo audioClipPitchInfo {"Pitch",-24,24,0,"st"};
inline constexpr ParamInfo audioClipReverseInfo{"Reverse",0,1,0,""};
inline constexpr ParamInfo audioClipXfadeInfo{"Loop XFade",0,100,5,"ms"};
inline constexpr ParamInfo compThresholdInfo  {"Threshold",-60,0,-18,"dB"};
inline constexpr ParamInfo compRatioInfo      {"Ratio",1,20,4,":1"};
inline constexpr ParamInfo compAttackInfo     {"Attack",.001f,1,.01f,"s"};
inline constexpr ParamInfo compReleaseInfo    {"Release",.01f,2,.15f,"s"};
inline constexpr ParamInfo compMakeupInfo     {"Makeup",0,2,1,""};
inline constexpr ParamInfo compMixInfo        {"Mix",0,1,1,""};
inline constexpr ParamInfo limiterThresholdInfo {"Threshold",-60,0,-6,"dB"};
inline constexpr ParamInfo limiterReleaseInfo   {"Release",.01f,2,.12f,"s"};
inline constexpr ParamInfo limiterCeilingInfo   {"Ceiling",-12,0,-.3f,"dB"};
inline constexpr ParamInfo limiterGainInfo      {"Gain",0,2,1,""};
inline constexpr ParamInfo chorusRateInfo  {"Rate",.05f,8,.6f,"Hz"};
inline constexpr ParamInfo chorusDepthInfo {"Depth",0,1,.35f,""};
inline constexpr ParamInfo chorusMixInfo   {"Mix",0,1,.35f,""};
inline constexpr ParamInfo chorusDelayInfo {"Delay",1,30,12,"ms"};
inline constexpr ParamInfo chorusFeedbackInfo {"Feedback",0,.85f,0,""};
inline constexpr ParamInfo quantizeRootInfo {"Root",0,11,0,""};
inline constexpr ParamInfo quantizeScaleInfo{"Scale",0,4,0,""};
inline constexpr ParamInfo quantizeModeInfo {"Mode",0,2,0,""};
inline constexpr ParamInfo tremoloRateInfo {"Rate",.1f,20,4,"Hz"};
inline constexpr ParamInfo tremoloDepthInfo{"Depth",0,1,.5f,""};
inline constexpr ParamInfo tremoloMixInfo  {"Mix",0,1,1,""};
inline constexpr ParamInfo tremoloStereoInfo{"Stereo",0,1,0,""};
inline constexpr ParamInfo tremoloSyncInfo{"Sync",0,1,0,""};
inline constexpr ParamInfo tremoloDivisionInfo{"Division",0,3,0,""};
inline constexpr ParamInfo crushBitsInfo   {"Bits",1,16,8,"bit"};
inline constexpr ParamInfo crushRateInfo   {"Rate",500,48000,8000,"Hz"};
inline constexpr ParamInfo crushMixInfo    {"Mix",0,1,1,""};
inline constexpr ParamInfo phaserRateInfo  {"Rate",.05f,8,.5f,"Hz"};
inline constexpr ParamInfo phaserDepthInfo {"Depth",0,1,.7f,""};
inline constexpr ParamInfo phaserFeedbackInfo{"Feedback",0,.9f,.3f,""};
inline constexpr ParamInfo phaserMixInfo   {"Mix",0,1,.5f,""};
inline constexpr ParamInfo randomChanceInfo{"Chance",0,1,.5f,""};
inline constexpr ParamInfo metronomeBPMInfo{"BPM",40,240,120,""};
inline constexpr ParamInfo metronomeLevelInfo{"Level",0,1,.4f,""};
inline constexpr ParamInfo metronomeAccentInfo{"Accent",0,1,1,""};
inline constexpr ParamInfo metronomeBeatsInfo{"Beats",1,16,4,""};
inline constexpr ParamInfo transposeSemitonesInfo{"Semitones",-24,24,0,""};
inline constexpr ParamInfo echoTimeInfo{"Time",.05f,2,.25f,"s"};
inline constexpr ParamInfo echoRepeatsInfo{"Repeats",1,8,3,""};
inline constexpr ParamInfo echoFeedbackInfo{"Feedback",0,1,.5f,""};
inline constexpr ParamInfo echoPitchInfo{"Pitch",-12,12,0,""};
inline constexpr ParamInfo gateThresholdInfo{"Threshold",-80,0,-40,"dB"};
inline constexpr ParamInfo gateAttackInfo{"Attack",.001f,1,.005f,"s"};
inline constexpr ParamInfo gateReleaseInfo{"Release",.01f,2,.15f,"s"};
inline constexpr ParamInfo gateRangeInfo{"Range",0,1,.8f,""};
inline constexpr ParamInfo midiOutChannelInfo{"Channel",0,16,0,""};
inline constexpr ParamInfo outputWidthInfo{"Width",0,2,1,""};
inline constexpr ParamInfo delaySyncInfo{"Sync",0,1,0,""};
inline constexpr ParamInfo delayDivisionInfo{"Division",0,3,1,""};
inline constexpr ParamInfo channelFilterInfo{"Channel",0,16,0,""};
inline constexpr ParamInfo arrangerVoiceInfo[3][3] = {
    {{"Minimum",0,127,72,"MIDI"},{"Maximum",0,127,95,"MIDI"},{"Texture",0,3,0,""}},
    {{"Minimum",0,127,55,"MIDI"},{"Maximum",0,127,71,"MIDI"},{"Texture",0,3,1,""}},
    {{"Minimum",0,127,36,"MIDI"},{"Maximum",0,127,54,"MIDI"},{"Texture",0,3,2,""}}
};

// The global parameter table. IDs are the persisted identity; keys are the
// readable alias used by v4 documents. Reservation: never reuse an ID or key.
inline const std::map<int,ParamSpec>& paramSpecs() {
    static const std::map<int,ParamSpec> table = []{
        std::map<int,ParamSpec> m;
        auto add=[&](int id,const ParamInfo& info,const char* key,bool discrete){m.emplace(id,ParamSpec{&info,key,discrete});};
        // 0..87 — legacy AU parameters. Values live in LegacyEngine::params.
        static const struct { int id; const char* key; bool discrete; } legacy[] = {
            {0,"osc1.wave",true},{1,"osc1.level",false},{2,"osc1.tune",true},
            {3,"osc2.wave",true},{4,"osc2.level",false},{5,"osc2.tune",true},
            {6,"osc3.wave",true},{7,"osc3.level",false},{8,"osc3.tune",true},
            {9,"envelope.attack",false},{10,"envelope.decay",false},{11,"envelope.sustain",false},
            {12,"envelope.release",false},{13,"input.gain",false},{14,"delay.time",false},
            {15,"delay.feedback",false},{16,"delay.mix",false},{17,"eq.low",false},
            {18,"eq.mid",false},{19,"eq.high",false},{20,"arp.enabled",true},
            {21,"arp.pattern",true},{22,"tempo",false},{23,"arp.gate",false},
            {24,"output.master",false},{25,"osc1.attack",false},{26,"osc1.sustain",false},
            {27,"osc1.release",false},{28,"osc2.attack",false},{29,"osc2.sustain",false},
            {30,"osc2.release",false},{31,"osc3.attack",false},{32,"osc3.sustain",false},
            {33,"osc3.release",false},{34,"arp.repeat",true},{35,"route.arp.notes",true},
            {36,"route.osc1.notes",true},{37,"route.osc2.notes",true},{38,"route.osc3.notes",true},
            {39,"route.mix.1",true},{40,"route.mix.2",true},{41,"route.mix.3",true},
            {42,"route.mix.4",true},{43,"route.eq.audio",true},{44,"route.delay.audio",true},
            {45,"route.output.audio",true},{46,"route.chord.notes",true},{47,"route.velocity.notes",true},
            {48,"route.arp.clock",true},{49,"route.progression.clock",true},{50,"route.velocity.clock",true},
            {51,"chord.root",true},{52,"chord.quality",true},{53,"chord.run",true},
            {54,"progression.run",true},{55,"progression.length",true},{56,"progression.bars",true},
            {57,"clock.bpm",false},{58,"clock.syncHost",true},{59,"velocity.minimum",true},
            {60,"velocity.maximum",true},{61,"velocity.beats",false},{62,"velocity.shape",true},
            {63,"progression.root.1",true},{64,"progression.quality.1",true},
            {65,"progression.root.2",true},{66,"progression.quality.2",true},
            {67,"progression.root.3",true},{68,"progression.quality.3",true},
            {69,"progression.root.4",true},{70,"progression.quality.4",true},
            {71,"progression.root.5",true},{72,"progression.quality.5",true},
            {73,"progression.root.6",true},{74,"progression.quality.6",true},
            {75,"progression.root.7",true},{76,"progression.quality.7",true},
            {77,"progression.root.8",true},{78,"progression.quality.8",true},
            {79,"route.clip.clock",true},{80,"route.model.notes",true},{81,"clip.run",true},
            {82,"clip.loop",true},{83,"midi.localOnly",true},{84,"route.midiOut.notes",true},
            {85,"trigger.pulse",true},{86,"wait.seconds",false},{87,"trigger.count",true},
        };
        for(const auto& row:legacy) m.emplace(row.id,ParamSpec{&params[row.id],row.key,row.discrete});
        // Arranger voice ranges (1024..1032).
        static const char* voiceKeys[3][3] = {
            {"high.minimum","high.maximum","high.texture"},
            {"middle.minimum","middle.maximum","middle.texture"},
            {"low.minimum","low.maximum","low.texture"}
        };
        for(int v=0;v<3;++v) for(int c=0;c<3;++c)
            m.emplace(ArrHighMin+3*v+c,ParamSpec{&arrangerVoiceInfo[v][c],voiceKeys[v][c],true});
        // Audio and MIDI processor parameters.
        add(FilterType,filterTypeInfo,"filter.type",true);
        add(FilterCutoff,filterCutoffInfo,"filter.cutoff",false);
        add(FilterResonance,filterResonanceInfo,"filter.resonance",false);
        add(FilterDrive,filterDriveInfo,"filter.drive",false);
        add(ReverbMix,reverbMixInfo,"reverb.mix",false);
        add(ReverbSize,reverbSizeInfo,"reverb.size",false);
        add(ReverbDamping,reverbDampingInfo,"reverb.damping",false);
        add(ReverbWidth,reverbWidthInfo,"reverb.width",false);
        add(ReverbPreDelay,reverbPreDelayInfo,"reverb.preDelay",false);
        add(NoiseLevel,noiseLevelInfo,"noise.level",false);
        add(NoiseType,noiseTypeInfo,"noise.type",true);
        add(SamplerRoot,samplerRootInfo,"sampler.root",false);
        add(SamplerGain,samplerGainInfo,"sampler.gain",false);
        add(SamplerLoop,samplerLoopInfo,"sampler.loop",true);
        add(SamplerAttack,samplerAttackInfo,"sampler.attack",false);
        add(SamplerRelease,samplerReleaseInfo,"sampler.release",false);
        add(SamplerStart,samplerStartInfo,"sampler.start",false);
        add(SamplerEnd,samplerEndInfo,"sampler.end",false);
        add(SamplerReverse,samplerReverseInfo,"sampler.reverse",true);
        add(SamplerVelocity,samplerVelocityInfo,"sampler.velocity",false);
        add(SamplerXfade,samplerXfadeInfo,"sampler.loopXfade",false);
        add(AudioClipGain,audioClipGainInfo,"audioClip.gain",false);
        add(AudioClipLoop,audioClipLoopInfo,"audioClip.loop",true);
        add(AudioClipStart,audioClipStartInfo,"audioClip.start",false);
        add(AudioClipEnd,audioClipEndInfo,"audioClip.end",false);
        add(AudioClipFade,audioClipFadeInfo,"audioClip.fade",false);
        add(AudioClipPitch,audioClipPitchInfo,"audioClip.pitch",false);
        add(AudioClipReverse,audioClipReverseInfo,"audioClip.reverse",true);
        add(AudioClipXfade,audioClipXfadeInfo,"audioClip.loopXfade",false);
        add(CompThreshold,compThresholdInfo,"compressor.threshold",false);
        add(CompRatio,compRatioInfo,"compressor.ratio",false);
        add(CompAttack,compAttackInfo,"compressor.attack",false);
        add(CompRelease,compReleaseInfo,"compressor.release",false);
        add(CompMakeup,compMakeupInfo,"compressor.makeup",false);
        add(CompMix,compMixInfo,"compressor.mix",false);
        add(LimiterThreshold,limiterThresholdInfo,"limiter.threshold",false);
        add(LimiterRelease,limiterReleaseInfo,"limiter.release",false);
        add(LimiterCeiling,limiterCeilingInfo,"limiter.ceiling",false);
        add(LimiterGain,limiterGainInfo,"limiter.gain",false);
        add(ChorusRate,chorusRateInfo,"chorus.rate",false);
        add(ChorusDepth,chorusDepthInfo,"chorus.depth",false);
        add(ChorusMix,chorusMixInfo,"chorus.mix",false);
        add(ChorusDelay,chorusDelayInfo,"chorus.delay",false);
        add(ChorusFeedback,chorusFeedbackInfo,"chorus.feedback",false);
        add(QuantizeRoot,quantizeRootInfo,"quantize.root",true);
        add(QuantizeScale,quantizeScaleInfo,"quantize.scale",true);
        add(QuantizeMode,quantizeModeInfo,"quantize.mode",true);
        add(TremoloRate,tremoloRateInfo,"tremolo.rate",false);
        add(TremoloDepth,tremoloDepthInfo,"tremolo.depth",false);
        add(TremoloMix,tremoloMixInfo,"tremolo.mix",false);
        add(TremoloStereo,tremoloStereoInfo,"tremolo.stereo",false);
        add(TremoloSync,tremoloSyncInfo,"tremolo.sync",true);
        add(TremoloDivision,tremoloDivisionInfo,"tremolo.division",true);
        add(CrushBits,crushBitsInfo,"crush.bits",true);
        add(CrushRate,crushRateInfo,"crush.rate",false);
        add(CrushMix,crushMixInfo,"crush.mix",false);
        add(PhaserRate,phaserRateInfo,"phaser.rate",false);
        add(PhaserDepth,phaserDepthInfo,"phaser.depth",false);
        add(PhaserFeedback,phaserFeedbackInfo,"phaser.feedback",false);
        add(PhaserMix,phaserMixInfo,"phaser.mix",false);
        add(RandomChance,randomChanceInfo,"random.chance",false);
        add(MetronomeBPM,metronomeBPMInfo,"metronome.bpm",false);
        add(MetronomeLevel,metronomeLevelInfo,"metronome.level",false);
        add(MetronomeAccent,metronomeAccentInfo,"metronome.accent",false);
        add(MetronomeBeats,metronomeBeatsInfo,"metronome.beats",true);
        add(TransposeSemitones,transposeSemitonesInfo,"transpose.semitones",true);
        add(EchoTime,echoTimeInfo,"echo.time",false);
        add(EchoRepeats,echoRepeatsInfo,"echo.repeats",true);
        add(EchoFeedback,echoFeedbackInfo,"echo.feedback",false);
        add(EchoPitch,echoPitchInfo,"echo.pitch",true);
        add(GateThreshold,gateThresholdInfo,"gate.threshold",false);
        add(GateAttack,gateAttackInfo,"gate.attack",false);
        add(GateRelease,gateReleaseInfo,"gate.release",false);
        add(GateRange,gateRangeInfo,"gate.range",false);
        add(MidiOutChannel,midiOutChannelInfo,"midiOut.channel",true);
        add(OutputWidth,outputWidthInfo,"output.width",false);
        add(DelaySync,delaySyncInfo,"delay.sync",true);
        add(DelayDivision,delayDivisionInfo,"delay.division",true);
        add(ChannelFilter,channelFilterInfo,"channelFilter.channel",true);
        return m;
    }();
    return table;
}
inline const ParamSpec* paramSpec(int id) {
    const auto& table=paramSpecs();
    auto it=table.find(id);
    return it==table.end()?nullptr:&it->second;
}
inline const char* paramKey(int id) {
    const ParamSpec* spec=paramSpec(id);
    return spec?spec->key:"";
}
inline int paramIdForKey(const char* key) {
    if(!key||!*key)return -1;
    for(const auto& row:paramSpecs()) if(std::strcmp(row.second.key,key)==0) return row.first;
    return -1;
}

// ---------------------------------------------------------------------------
// Node descriptors
// ---------------------------------------------------------------------------
struct NodeSpec {
    const char* key;
    int type;
    const char* title;
    const char* category;
    uint16_t nodeVersion=1;
    uint8_t stateVersion=1;
    bool visible=true;
    bool instantiable=true;
    uint32_t capabilities=0;
    TimeMode timeMode=TimeMode::None;
    int latencyFrames=0;
    std::vector<PortSpec> inputs;
    std::vector<PortSpec> outputs;
    std::vector<int> params;
    const char* assetKind=nullptr;
};

namespace detail {
inline PortSpec pin(const char* id,const char* label,Signal s,PortRole r=RoleData,uint8_t f=PortOptional) {
    // Every causal trigger input accepts several distinct sources; identical
    // duplicates are rejected by the compiler instead of silently doubling.
    if(r==RoleTrigger) f|=PortMultiSource;
    return PortSpec{id,label,s,r,f};
}
inline PortSpec pout(const char* id,const char* label,Signal s,PortRole r=RoleData) {
    return PortSpec{id,label,s,r,0};
}
}

// The registry. Insertion order is the palette order.
inline const std::vector<NodeSpec>& nodeSpecs() {
    using namespace detail;
    static const std::vector<NodeSpec> table = []{
        std::vector<NodeSpec> v;
        auto add=[&](NodeSpec s){v.push_back(std::move(s));};
        const uint8_t req=0, trig=PortOptional|PortFeedback, multi=PortMultiSource|PortOptional;
        // ---- inputs -------------------------------------------------------
        add({"soundnet.midi-input",MidiNode,"本地 MIDI","输入",1,1,true,true,
             CapNoteSource|CapTriggerSource|CapGenerator,TimeMode::None,0,
             {}, {pout("notes","音符",Signal::Midi,RoleMain)}, {}, nullptr});
        add({"soundnet.daw-midi-input",DawMidiNode,"DAW MIDI","输入",1,1,true,true,
             CapNoteSource|CapTriggerSource|CapGenerator,TimeMode::None,0,
             {}, {pout("notes","音符",Signal::Midi,RoleMain)}, {}, nullptr});
        add({"soundnet.midi-clip",ClipNode,"MIDI Clip","输入",1,1,true,true,
             CapNoteSource|CapData|CapAssetClip|CapStream,TimeMode::Timed,0,
             {pin("clock","时钟",Signal::Clock,RoleClock),pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional|PortFeedback)},
             {pout("notes","音符",Signal::Midi,RoleMain),pout("cycle","循环完成",Signal::Trigger,RoleTrigger)},
             {ClipRun,ClipLoop}, "clip"});
        add({"soundnet.audio-input",InputNode,"硬件音频输入","输入",1,1,true,true,
             CapStream|CapGenerator,TimeMode::Stream,0,
             {}, {pout("audio","音频",Signal::Audio,RoleMain)}, {InputGain}, nullptr});
        add({"soundnet.daw-audio-input",DawAudioNode,"DAW 音频输入","输入",1,1,true,true,
             CapStream|CapGenerator,TimeMode::Stream,0,
             {}, {pout("audio","音频",Signal::Audio,RoleMain)}, {InputGain}, nullptr});
        add({"soundnet.chord",ChordNode,"和弦输入","输入",1,1,true,true,
             CapNoteSource|CapData|CapGenerator,TimeMode::Timed,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {ChordRun,ChordRoot,ChordQuality}, nullptr});
        add({"soundnet.progression",ProgressNode,"和声走向","输入",1,1,true,true,
             CapNoteSource|CapData|CapClockSource,TimeMode::Timed,0,
             {pin("clock","时钟",Signal::Clock,RoleClock),pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional|PortFeedback)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {ProgressRun,ProgressLength,ProgressBars,
              ProgressRoot1,ProgressQuality1,ProgressRoot2,ProgressQuality2,
              ProgressRoot3,ProgressQuality3,ProgressRoot4,ProgressQuality4,
              ProgressRoot5,ProgressQuality5,ProgressRoot6,ProgressQuality6,
              ProgressRoot7,ProgressQuality7,ProgressRoot8,ProgressQuality8}, nullptr});
        add({"soundnet.clock",ClockNode,"节奏速度","输入",1,1,true,true,
             CapClockSource|CapGenerator,TimeMode::Timed,0,
             {}, {pout("clock","时钟",Signal::Clock,RoleClock)}, {ClockBPM,ClockSync}, nullptr});
        add({"soundnet.daw-clock",DawClockNode,"DAW 时钟","输入",1,1,true,true,
             CapClockSource|CapGenerator,TimeMode::Timed,0,
             {}, {pout("clock","时钟",Signal::Clock,RoleClock)}, {ClockBPM,ClockSync}, nullptr});
        // ---- processors ---------------------------------------------------
        add({"soundnet.oscillator",Osc1Node,"振荡器","处理",1,1,true,true,
             CapStream,TimeMode::Stream,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {Wave1,Level1,Tune1,Attack,Decay,Sustain,Release}, nullptr});
        add({"soundnet.arp",ArpNode,"ARP","处理",1,1,true,true,
             CapNoteSource|CapData,TimeMode::Timed,0,
             {pin("notes","音符",Signal::Midi,RoleMain),pin("clock","时钟",Signal::Clock,RoleClock),
              pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional|PortFeedback)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {ArpOn,Pattern,ArpRepeat,Gate}, nullptr});
        add({"soundnet.voice-arranger",ArrangerNode,"三声部编排器","处理",1,1,true,true,
             CapNoteSource|CapData|CapMultiOutput,TimeMode::Timed,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("high","高声部",Signal::Midi,RoleMain),pout("middle","中声部",Signal::Midi,RoleMain),
              pout("low","低声部",Signal::Midi,RoleMain)},
             {ArrHighMin,ArrHighMax,ArrHighTexture,ArrMidMin,ArrMidMax,ArrMidTexture,
              ArrLowMin,ArrLowMax,ArrLowTexture}, nullptr});
        add({"soundnet.velocity",VelocityNode,"力度自动化","处理",1,1,true,true,
             CapNoteSource|CapData,TimeMode::Timed,0,
             {pin("notes","音符",Signal::Midi,RoleMain),pin("clock","时钟",Signal::Clock,RoleClock)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {VelocityMin,VelocityMax,VelocityRate,VelocityShape}, nullptr});
        add({"soundnet.note-model",ModelNode,"音符模型","处理",1,1,true,true,
             CapNoteSource|CapData|CapAssetModel,TimeMode::None,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)}, {}, "model"});
        add({"soundnet.mixer",MixNode,"混音器","处理",1,1,true,true,
             CapStream,TimeMode::Stream,0,
             {pin("in1","输入 1",Signal::Audio,RoleData,multi),pin("in2","输入 2",Signal::Audio,RoleData,multi),
              pin("in3","输入 3",Signal::Audio,RoleData,multi),pin("in4","输入 4",Signal::Audio,RoleData,multi)},
             {pout("audio","音频",Signal::Audio,RoleMain)}, {}, nullptr});
        add({"soundnet.eq",EQNode,"EQ","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {EQLow,EQMid,EQHigh}, nullptr});
        add({"soundnet.delay",DelayNode,"Delay","处理",1,1,true,true,
             CapStream|CapBypassable|CapTemporal,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {DelayTime,DelayFeedback,DelayMix,DelaySync,DelayDivision}, nullptr});
        add({"soundnet.filter",FilterNode,"滤波器","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {FilterType,FilterCutoff,FilterResonance,FilterDrive}, nullptr});
        add({"soundnet.reverb",ReverbNode,"混响","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {ReverbMix,ReverbSize,ReverbDamping,ReverbWidth,ReverbPreDelay}, nullptr});
        add({"soundnet.noise",NoiseNode,"噪声","处理",1,1,true,true,
             CapStream|CapGenerator|CapBypassable,TimeMode::None,0,
             {}, {pout("audio","音频",Signal::Audio,RoleMain)},
             {NoiseLevel,NoiseType}, nullptr});
        add({"soundnet.sampler",SamplerNode,"采样器","处理",1,1,true,true,
             CapStream|CapBypassable|CapAssetAudio,TimeMode::Stream,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {SamplerRoot,SamplerGain,SamplerLoop,SamplerAttack,SamplerRelease,SamplerStart,
              SamplerEnd,SamplerReverse,SamplerVelocity,SamplerXfade}, "audio"});
        add({"soundnet.audio-clip",AudioClipNode,"音频片段","处理",1,1,true,true,
             CapStream|CapBypassable|CapAssetAudio,TimeMode::Timed,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional|PortFeedback)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {AudioClipGain,AudioClipLoop,AudioClipStart,AudioClipEnd,AudioClipFade,
              AudioClipPitch,AudioClipReverse,AudioClipXfade}, "audio"});
        add({"soundnet.compressor",CompressorNode,"压缩器","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req),pin("sidechain","侧链",Signal::Audio,RoleSidechain)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {CompThreshold,CompRatio,CompAttack,CompRelease,CompMakeup,CompMix}, nullptr});
        add({"soundnet.limiter",LimiterNode,"限幅器","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {LimiterThreshold,LimiterRelease,LimiterCeiling,LimiterGain}, nullptr});
        add({"soundnet.chorus",ChorusNode,"合唱","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {ChorusRate,ChorusDepth,ChorusMix,ChorusDelay,ChorusFeedback}, nullptr});
        add({"soundnet.scale-quantize",QuantizeNode,"音阶量化","处理",1,1,true,true,
             CapNoteSource|CapData|CapBypassable,TimeMode::None,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {QuantizeRoot,QuantizeScale,QuantizeMode}, nullptr});
        add({"soundnet.tremolo",TremoloNode,"颤音","处理",1,1,true,true,
             CapStream|CapBypassable|CapTemporal,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {TremoloRate,TremoloDepth,TremoloMix,TremoloStereo,TremoloSync,TremoloDivision}, nullptr});
        add({"soundnet.bitcrusher",BitcrusherNode,"位深破碎","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {CrushBits,CrushRate,CrushMix}, nullptr});
        add({"soundnet.phaser",PhaserNode,"相位器","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {PhaserRate,PhaserDepth,PhaserFeedback,PhaserMix}, nullptr});
        add({"soundnet.transpose",TransposeNode,"移调","处理",1,1,true,true,
             CapNoteSource|CapData|CapBypassable,TimeMode::None,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {TransposeSemitones}, nullptr});
        add({"soundnet.note-echo",EchoNode,"音符回声","处理",1,1,true,true,
             CapNoteSource|CapData|CapBypassable|CapTemporal,TimeMode::Timed,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {EchoTime,EchoRepeats,EchoFeedback,EchoPitch}, nullptr});
        add({"soundnet.noise-gate",GateNode,"噪声门","处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {GateThreshold,GateAttack,GateRelease,GateRange}, nullptr});
        add({"soundnet.channel-filter",ChannelFilterNode,"通道过滤","处理",1,1,true,true,
             CapNoteSource|CapData|CapBypassable,TimeMode::None,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)},
             {ChannelFilter}, nullptr});
        // ---- logic --------------------------------------------------------
        add({"soundnet.start",StartNode,"开始触发器","逻辑",1,1,true,true,
             CapTriggerSource|CapData,TimeMode::None,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger)},
             {pout("trigger","触发",Signal::Trigger,RoleTrigger)}, {TriggerPulse}, nullptr});
        add({"soundnet.stop",StopNode,"停止触发器","逻辑",1,1,true,true,
             CapTriggerSource|CapData,TimeMode::None,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger)},
             {pout("trigger","触发",Signal::Trigger,RoleTrigger)}, {TriggerPulse}, nullptr});
        add({"soundnet.wait",WaitNode,"等待","逻辑",1,1,true,true,
             CapTriggerSource|CapData|CapTemporal,TimeMode::Timed,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional|PortFeedback)},
             {pout("trigger","触发",Signal::Trigger,RoleTrigger)}, {WaitSeconds}, nullptr});
        add({"soundnet.count",CountNode,"次数条件","逻辑",1,1,true,true,
             CapTriggerSource|CapData,TimeMode::None,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger)},
             {pout("trigger","触发",Signal::Trigger,RoleTrigger)}, {TriggerCount}, nullptr});
        add({"soundnet.random",RandomNode,"随机触发","逻辑",1,1,true,true,
             CapTriggerSource|CapData,TimeMode::None,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger)},
             {pout("trigger","触发",Signal::Trigger,RoleTrigger)}, {RandomChance}, nullptr});
        add({"soundnet.metronome",MetronomeNode,"节拍器","逻辑",1,1,true,true,
             CapStream|CapGenerator|CapBypassable|CapTemporal,TimeMode::Timed,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {MetronomeBPM,MetronomeLevel,MetronomeAccent,MetronomeBeats}, nullptr});
        // ---- annotation and outputs --------------------------------------
        add({"soundnet.comment",CommentNode,"注释","注释",1,1,true,true,
             0,TimeMode::None,0, {}, {}, {}, nullptr});
        add({"soundnet.audio-output",HardwareOutputNode,"硬件音频输出","输出",1,1,true,true,
             CapOutputSink|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleOutput,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)}, {Master,OutputWidth}, nullptr});
        add({"soundnet.daw-audio-output",DawOutputNode,"DAW 音频输出","输出",1,1,true,true,
             CapOutputSink|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleOutput,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)}, {Master,OutputWidth}, nullptr});
        add({"soundnet.midi-output",MidiOutNode,"MIDI 输出","输出",1,1,true,true,
             CapNoteSource|CapData,TimeMode::None,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("notes","音符",Signal::Midi,RoleMain)}, {MidiOutChannel}, nullptr});
        add({"soundnet.master-output",OutputNode,"音频输出","输出",1,1,false,true,
             CapOutputSink|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleOutput,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)}, {Master,OutputWidth}, nullptr});
        // Migration-only legacy types: registered so old documents migrate, but
        // they can never be inserted or instantiated.
        add({"soundnet.legacy-osc2",Osc2Node,"OSC 2","迁移",1,1,false,false,0,TimeMode::Stream,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},{pout("audio","音频",Signal::Audio,RoleMain)}, {}, nullptr});
        add({"soundnet.legacy-osc3",Osc3Node,"OSC 3","迁移",1,1,false,false,0,TimeMode::Stream,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},{pout("audio","音频",Signal::Audio,RoleMain)}, {}, nullptr});
        add({"soundnet.disconnected",NoNode,"未连接","迁移",1,1,false,false,0,TimeMode::None,0,
             {},{},{}, nullptr});
        return v;
    }();
    return table;
}

inline const std::map<int,const NodeSpec*>& nodeSpecIndex() {
    static const std::map<int,const NodeSpec*> index = []{
        std::map<int,const NodeSpec*> m;
        // Type 0 means "no kernel": an unknown or missing node. It must not
        // resolve to the migration-only placeholder entry.
        for(const auto& spec:nodeSpecs()) if(spec.type!=NoNode) m.emplace(spec.type,&spec);
        return m;
    }();
    return index;
}
inline const std::map<std::string,const NodeSpec*>& nodeKeyIndex() {
    static const std::map<std::string,const NodeSpec*> index = []{
        std::map<std::string,const NodeSpec*> m;
        for(const auto& spec:nodeSpecs()) m.emplace(spec.key,&spec);
        return m;
    }();
    return index;
}

inline const NodeSpec* nodeSpec(int type) {
    const auto& index=nodeSpecIndex();
    auto it=index.find(type);
    return it==index.end()?nullptr:it->second;
}
inline const NodeSpec* nodeSpecForKey(const char* key) {
    if(!key||!*key)return nullptr;
    const auto& index=nodeKeyIndex();
    auto it=index.find(key);
    return it==index.end()?nullptr:it->second;
}
inline const char* nodeKey(int type) {
    const NodeSpec* spec=nodeSpec(type);
    return spec?spec->key:"";
}
inline int nodeTypeForKey(const char* key) {
    const NodeSpec* spec=nodeSpecForKey(key);
    return spec?spec->type:0;
}
// A type is valid when it is a registered, instantiable kernel. There is no
// numeric ceiling any more: type 47 registers like type 1 does.
inline bool validNodeType(int type,bool allowMigrationOnly=false) {
    const NodeSpec* spec=nodeSpec(type);
    return spec && (allowMigrationOnly || spec->instantiable);
}

// Port lookup. `port` is the descriptor position, which is also the persisted
// v3 index. Out-of-range access returns nullptr instead of reading past a table.
inline const PortSpec* nodePort(int type,bool input,size_t index) {
    const NodeSpec* spec=nodeSpec(type);
    if(!spec)return nullptr;
    const auto& ports=input?spec->inputs:spec->outputs;
    return index<ports.size()?&ports[index]:nullptr;
}
inline int nodePortIndex(int type,bool input,const char* id) {
    const NodeSpec* spec=nodeSpec(type);
    if(!spec||!id)return -1;
    const auto& ports=input?spec->inputs:spec->outputs;
    for(size_t i=0;i<ports.size();++i) if(std::strcmp(ports[i].id,id)==0) return int(i);
    return -1;
}
inline const char* nodePortId(int type,bool input,size_t index) {
    const PortSpec* port=nodePort(type,input,index);
    return port?port->id:"";
}
inline int nodeInputCount(int type) {
    const NodeSpec* spec=nodeSpec(type);
    return spec?int(spec->inputs.size()):0;
}
inline int nodeOutputCount(int type) {
    const NodeSpec* spec=nodeSpec(type);
    return spec?int(spec->outputs.size()):0;
}
// First input port with the given role, or -1. Roles replace the old habit of
// reading `inputs[0]` and guessing.
inline int nodeInputByRole(int type,PortRole role) {
    const NodeSpec* spec=nodeSpec(type);
    if(!spec)return -1;
    for(size_t i=0;i<spec->inputs.size();++i) if(spec->inputs[i].role==role) return int(i);
    return -1;
}
inline int nodeInputByRole(int type,PortRole role,Signal signal) {
    const NodeSpec* spec=nodeSpec(type);
    if(!spec)return -1;
    for(size_t i=0;i<spec->inputs.size();++i)
        if(spec->inputs[i].role==role && spec->inputs[i].signal==signal) return int(i);
    return -1;
}
inline const ParamSpec* nodeParamSpec(int type,int id) {
    const NodeSpec* spec=nodeSpec(type);
    if(!spec)return nullptr;
    for(int candidate:spec->params) if(candidate==id) return paramSpec(id);
    return nullptr;
}
inline bool nodeAcceptsParam(int type,int id) {return nodeParamSpec(type,id)!=nullptr;}
inline bool nodeParamDiscrete(int type,int id) {
    const ParamSpec* spec=nodeParamSpec(type,id);
    return spec && spec->discrete;
}
inline bool nodeHas(uint32_t capabilities,uint32_t flag) {return (capabilities&flag)!=0;}
inline uint32_t nodeCapabilities(int type) {
    const NodeSpec* spec=nodeSpec(type);
    return spec?spec->capabilities:0;
}
inline bool arrangerParam(int id) {return id>=ArrHighMin && id<=ArrLowTexture;}

// Self-check used by tests and by the editor at launch. Returns an empty string
// when the registry is internally consistent.
inline std::string registryProblems() {
    std::string why;
    std::map<std::string,int> keys;
    std::map<int,std::string> types;
    std::map<std::string,int> paramKeys;
    for(const auto& spec:nodeSpecs()) {
        if(!spec.key||!*spec.key) {why+="节点缺少 nodeKey。\n";continue;}
        if(!keys.emplace(spec.key,spec.type).second) why+="nodeKey 重复："+std::string(spec.key)+"\n";
        if(!types.emplace(spec.type,spec.key).second) why+="节点类型编号重复："+std::to_string(spec.type)+"\n";
        if(!spec.title||!*spec.title) why+=std::string(spec.key)+" 缺少标题。\n";
        if(!spec.category||!*spec.category) why+=std::string(spec.key)+" 缺少分类。\n";
        if(spec.nodeVersion==0) why+=std::string(spec.key)+" 的 nodeVersion 必须为正。\n";
        if(spec.instantiable && spec.outputs.empty() && spec.inputs.empty() &&
           spec.capabilities!=0 && spec.type!=CommentNode)
            why+=std::string(spec.key)+" 声明了能力却没有端口。\n";
        if(spec.instantiable && nodeHas(spec.capabilities,CapOutputSink) &&
           spec.inputs.empty()) why+=std::string(spec.key)+" 是输出汇聚节点却没有输入。\n";
        for(bool input:{true,false}) {
            std::map<std::string,int> ids;
            const auto& ports=input?spec.inputs:spec.outputs;
            for(const auto& port:ports) {
                if(!port.id||!*port.id) why+=std::string(spec.key)+" 存在空端口 ID。\n";
                if(!ids.emplace(port.id,1).second) why+=std::string(spec.key)+" 同方向端口 ID 重复："+port.id+"\n";
                if(port.signal==Signal::None) why+=std::string(spec.key)+" 端口 "+port.id+" 的信号为 None。\n";
            }
        }
        if(spec.instantiable) {
            if(spec.outputs.empty() && !nodeHas(spec.capabilities,CapOutputSink) &&
               spec.type!=CommentNode) why+=std::string(spec.key)+" 没有输出端口。\n";
            const bool source=nodeHas(spec.capabilities,CapGenerator)||nodeHas(spec.capabilities,CapNoteSource)||
                              nodeHas(spec.capabilities,CapClockSource)||nodeHas(spec.capabilities,CapStream);
            // Comments are deliberately inert: no ports, no capabilities.
            if(spec.type==CommentNode)continue;
            if(spec.inputs.empty() && !source)
                why+=std::string(spec.key)+" 既无输入也无生成能力。\n";
        }
        std::map<int,int> paramIds;
        for(int id:spec.params) {
            const ParamSpec* param=paramSpec(id);
            if(!param) {why+=std::string(spec.key)+" 引用了不存在的参数 "+std::to_string(id)+"。\n";continue;}
            if(!paramIds.emplace(id,1).second) why+=std::string(spec.key)+" 参数重复："+param->key+"\n";
            if(!param->key||!*param->key) why+=std::string(spec.key)+" 的参数缺少稳定键。\n";
            auto inserted=paramKeys.emplace(param->key,id);
            if(!inserted.second && inserted.first->second!=id)
                why+="参数键重复："+std::string(param->key)+"（"+std::to_string(inserted.first->second)+
                     " 与 "+std::to_string(id)+"）\n";
            const ParamInfo& info=*param->info;
            if(!(info.min<=info.def && info.def<=info.max)) why+=std::string(param->key)+" 的默认值越界。\n";
            if(param->discrete && (info.min!=std::trunc(info.min)||info.max!=std::trunc(info.max)||
                                   info.def!=std::trunc(info.def)))
                why+=std::string(param->key)+" 标记为离散但范围含小数。\n";
        }
        if(spec.assetKind) {
            const bool declared=nodeHas(spec.capabilities,CapAssetClip)||nodeHas(spec.capabilities,CapAssetAudio)||
                                nodeHas(spec.capabilities,CapAssetModel);
            if(!declared) why+=std::string(spec.key)+" 有素材类型但未声明素材能力。\n";
        }
    }
    return why;
}
} // namespace tri
