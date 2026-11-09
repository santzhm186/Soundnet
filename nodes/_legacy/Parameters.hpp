#pragma once
#include "Declarations.hpp"
namespace tri {
inline const std::map<int,ParamSpec>& legacyParameterSpecs() {
    static const std::map<int,ParamSpec> table = []{
        std::map<int,ParamSpec> m;
        auto add=[&](int id,const ParamInfo& info,const char* key,bool discrete){registerParameter(m,id,ParamSpec{&info,key,discrete});};
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
        for(const auto& row:legacy) registerParameter(m,row.id,ParamSpec{&params[row.id],row.key,row.discrete});
        // Arranger voice ranges (1024..1032).
        static const char* voiceKeys[3][3] = {
            {"high.minimum","high.maximum","high.texture"},
            {"middle.minimum","middle.maximum","middle.texture"},
            {"low.minimum","low.maximum","low.texture"}
        };
        for(int v=0;v<3;++v) for(int c=0;c<3;++c)
            registerParameter(m,ArrHighMin+3*v+c,ParamSpec{&arrangerVoiceInfo[v][c],voiceKeys[v][c],true});
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
        add(AiMusicBackend,aiMusicBackendInfo,"ai.backend",true);
        add(AiMusicModel,aiMusicModelInfo,"ai.model",true);
        add(AiMusicDuration,aiMusicDurationInfo,"ai.duration",false);
        add(AiMusicSeed,aiMusicSeedInfo,"ai.seed",true);
        add(AiMusicQuality,aiMusicQualityInfo,"ai.quality",true);
        add(AiMusicOutMode,aiMusicOutModeInfo,"ai.outMode",true);
        // Enumerated parameters. Order is the stored value.
        static const char* const waves[]={"正弦","方波","锯齿","三角"};
        static const char* const patterns[]={"直八","Bossa Nova","Reggae","Disco","十六分脉冲"};
        static const char* const qualities[]={"大三和弦","小三和弦","属七","大七","小七","减三","挂二","挂四"};
        static const char* const filterTypes[]={"低通","高通","带通"};
        static const char* const noiseTypes[]={"白噪声","粉红噪声"};
        static const char* const scales[]={"大调","小调","多利亚","五声","半音"};
        static const char* const quantizeModes[]={"就近","向上","向下"};
        static const char* const roots[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        static const char* const onOff[]={"关闭","开启"};
        static const char* const clockSync[]={"自由","同步宿主"};
        static const char* const tremoloDivisions[]={"四分","八分","十六分","附点四分"};
        static const char* const delayDivisions[]={"四分","八分","十六分","附点八分"};
        static const char* const aiBackends[]={"audio.cpp 命令行","audio.cpp 服务","ComfyUI"};
        static const char* const aiModels[]={"ACE-Step 1.5","YuE2 BF16","YuE2 INT8","YuE2 GGUF Q4"};
        static const char* const aiQualities[]={"快速","均衡","精细"};
        static const char* const aiOutModes[]={"仅音频","音频与乐谱","仅乐谱"};
        auto option=[&](int id,const char* const* labels,uint8_t count) {
            auto it=m.find(id);
            if(it==m.end())return;
            it->second.options=labels;
            it->second.optionCount=count;
        };
        for(int id:{Wave1,Wave2,Wave3})option(id,waves,4);
        option(Pattern,patterns,5);
        option(ChordQuality,qualities,8);
        option(FilterType,filterTypes,3);
        option(NoiseType,noiseTypes,2);
        option(QuantizeScale,scales,5);
        option(QuantizeMode,quantizeModes,3);
        option(QuantizeRoot,roots,12);
        for(int id:{int(ArpOn),int(ClipRun),int(ClipLoop),int(ChordRun),int(ProgressRun),
                    SamplerLoop,SamplerReverse,AudioClipLoop,AudioClipReverse,TremoloSync,DelaySync})
            option(id,onOff,2);
        option(ClockSync,clockSync,2);
        option(TremoloDivision,tremoloDivisions,4);
        option(DelayDivision,delayDivisions,4);
        option(AiMusicBackend,aiBackends,3);
        option(AiMusicModel,aiModels,4);
        option(AiMusicQuality,aiQualities,3);
        option(AiMusicOutMode,aiOutModes,3);
        auto group=[&](ParamGroup value,std::initializer_list<int> ids) {
            for(int id:ids) {auto it=m.find(id);if(it!=m.end())it->second.group=value;}
        };
        group(ParamGroup::Tone,{Wave1,Tune1,Wave2,Tune2,Wave3,Tune3,FilterType,FilterCutoff,FilterResonance,
            FilterDrive,NoiseType,SamplerRoot,AudioClipPitch,CrushBits,CrushRate,TransposeSemitones,EchoPitch,EQLow,EQMid,EQHigh});
        group(ParamGroup::Envelope,{Attack,Decay,Sustain,Release,SamplerAttack,SamplerRelease,AudioClipFade,SamplerXfade,AudioClipXfade});
        group(ParamGroup::Playback,{ClipRun,ClipLoop,ChordRun,ProgressRun,ArpOn,SamplerLoop,SamplerStart,SamplerEnd,
            SamplerReverse,AudioClipLoop,AudioClipStart,AudioClipEnd,AudioClipReverse});
        group(ParamGroup::Timing,{ClockBPM,ClockSync,Tempo,Pattern,Gate,ArpRepeat,ProgressLength,ProgressBars,
            WaitSeconds,TriggerCount,DelayTime,DelaySync,DelayDivision,TremoloSync,TremoloDivision,
            MetronomeBPM,MetronomeBeats,EchoTime,EchoRepeats,VelocityRate});
        group(ParamGroup::Modulation,{ChorusRate,ChorusDepth,ChorusDelay,ChorusFeedback,TremoloRate,TremoloDepth,
            TremoloStereo,PhaserRate,PhaserDepth,PhaserFeedback,ReverbSize,ReverbDamping,ReverbWidth,ReverbPreDelay,
            DelayFeedback,EchoFeedback,VelocityShape});
        group(ParamGroup::Mix,{Level1,Level2,Level3,InputGain,Master,OutputWidth,NoiseLevel,SamplerGain,
            SamplerVelocity,AudioClipGain,CompMakeup,CompMix,LimiterGain,ChorusMix,TremoloMix,PhaserMix,ReverbMix,
            DelayMix,CrushMix,MetronomeLevel,MetronomeAccent,VelocityMin,VelocityMax});
        group(ParamGroup::Dynamics,{CompThreshold,CompRatio,CompAttack,CompRelease,LimiterThreshold,LimiterCeiling,
            LimiterRelease,GateThreshold,GateAttack,GateRelease,GateRange});
        group(ParamGroup::Harmony,{ChordRoot,ChordQuality,QuantizeRoot,QuantizeScale,QuantizeMode});
        for(int id=ProgressRoot1;id<=ProgressQuality8;++id)m.at(id).group=ParamGroup::Harmony;
        for(int id=ArrHighMin;id<=ArrLowTexture;++id)m.at(id).group=ParamGroup::Harmony;
        return m;
    }();
    return table;
}

} // namespace tri
