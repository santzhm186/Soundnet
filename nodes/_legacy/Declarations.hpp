#pragma once
// Shared declarations for nodes still using the original built-in kernel.
// New node modules use NodeDescriptor.hpp and node-local parameterID().
#include "../../platform/NodeDescriptor.hpp"
#include "../../src/LegacyEngine.hpp"
namespace tri {
// ---------------------------------------------------------------------------
// Node type IDs
// ---------------------------------------------------------------------------
// Legacy engine nodes (see LegacyEngine.hpp for 1..18).
inline constexpr int AudioOutputNode=19;
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
// AI music generation: submits an external, long-running task (CapTask) and
// turns its result into an embedded audio asset.
inline constexpr int AiMusicNode=FirstAppendedNode;

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
// AI music generation. Numeric parameters only; prompt/lyrics/executable path
// are text parameters declared by NodeSpec::texts (no numeric identity).
inline constexpr int AiMusicBackend=1310, AiMusicModel=1311, AiMusicDuration=1312;
inline constexpr int AiMusicSeed=1313, AiMusicQuality=1314, AiMusicOutMode=1315;
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
inline constexpr ParamInfo aiMusicBackendInfo{"Backend",0,2,0,""};
inline constexpr ParamInfo aiMusicModelInfo{"Model",0,3,0,""};
inline constexpr ParamInfo aiMusicDurationInfo{"Duration",10,600,120,"s"};
inline constexpr ParamInfo aiMusicSeedInfo{"Seed",0,999999,0,""};
inline constexpr ParamInfo aiMusicQualityInfo{"Quality",0,2,1,""};
inline constexpr ParamInfo aiMusicOutModeInfo{"Output",0,2,0,""};
inline constexpr ParamInfo arrangerVoiceInfo[3][3] = {
    {{"Minimum",0,127,72,"MIDI"},{"Maximum",0,127,95,"MIDI"},{"Texture",0,3,0,""}},
    {{"Minimum",0,127,55,"MIDI"},{"Maximum",0,127,71,"MIDI"},{"Texture",0,3,1,""}},
    {{"Minimum",0,127,36,"MIDI"},{"Maximum",0,127,54,"MIDI"},{"Texture",0,3,2,""}}
};


} // namespace tri
