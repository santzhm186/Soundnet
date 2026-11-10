#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::tremolo {
inline constexpr int typeID=nodeTypeID("soundnet.tremolo");
inline constexpr int TremoloRate=parameterID("soundnet.tremolo","tremolo.rate");
inline constexpr ParamInfo TremoloRateInfo{"Rate",0.1f,20.0f,4.0f,"Hz"};
inline constexpr int TremoloDepth=parameterID("soundnet.tremolo","tremolo.depth");
inline constexpr ParamInfo TremoloDepthInfo{"Depth",0.0f,1.0f,0.5f,""};
inline constexpr int TremoloMix=parameterID("soundnet.tremolo","tremolo.mix");
inline constexpr ParamInfo TremoloMixInfo{"Mix",0.0f,1.0f,1.0f,""};
inline constexpr int TremoloStereo=parameterID("soundnet.tremolo","tremolo.stereo");
inline constexpr ParamInfo TremoloStereoInfo{"Stereo",0.0f,1.0f,0.0f,""};
inline constexpr int TremoloSync=parameterID("soundnet.tremolo","tremolo.sync");
inline constexpr ParamInfo TremoloSyncInfo{"Sync",0.0f,1.0f,0.0f,""};
inline constexpr const char* TremoloSyncOptions[]={"关闭","开启"};
inline constexpr int TremoloDivision=parameterID("soundnet.tremolo","tremolo.division");
inline constexpr ParamInfo TremoloDivisionInfo{"Division",0.0f,3.0f,0.0f,""};
inline constexpr const char* TremoloDivisionOptions[]={"四分","八分","十六分","附点四分"};
inline constexpr int ClockBPM=parameterID("soundnet.tremolo","clock.bpm");
inline constexpr ParamInfo ClockBPMInfo{"Clock BPM",40.0f,240.0f,112.0f,"BPM"};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{TremoloRate,ParamSpec{&TremoloRateInfo,"tremolo.rate",false,nullptr,0,ParamGroup(5)}},{TremoloDepth,ParamSpec{&TremoloDepthInfo,"tremolo.depth",false,nullptr,0,ParamGroup(5)}},{TremoloMix,ParamSpec{&TremoloMixInfo,"tremolo.mix",false,nullptr,0,ParamGroup(6)}},{TremoloStereo,ParamSpec{&TremoloStereoInfo,"tremolo.stereo",false,nullptr,0,ParamGroup(5)}},{TremoloSync,ParamSpec{&TremoloSyncInfo,"tremolo.sync",true,TremoloSyncOptions,2,ParamGroup(4)}},{TremoloDivision,ParamSpec{&TremoloDivisionInfo,"tremolo.division",true,TremoloDivisionOptions,4,ParamGroup(4)}},{ClockBPM,ParamSpec{&ClockBPMInfo,"clock.bpm",false,nullptr,0,ParamGroup(4)}}};}
}
