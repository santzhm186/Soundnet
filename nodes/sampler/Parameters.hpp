#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::sampler {
inline constexpr int typeID=nodeTypeID("soundnet.sampler");
inline constexpr int SamplerRoot=parameterID("soundnet.sampler","sampler.root");
inline constexpr ParamInfo SamplerRootInfo{"Root",0.0f,127.0f,60.0f,"MIDI"};
inline constexpr int SamplerGain=parameterID("soundnet.sampler","sampler.gain");
inline constexpr ParamInfo SamplerGainInfo{"Gain",0.0f,1.0f,0.8f,""};
inline constexpr int SamplerLoop=parameterID("soundnet.sampler","sampler.loop");
inline constexpr ParamInfo SamplerLoopInfo{"Loop",0.0f,1.0f,0.0f,""};
inline constexpr const char* SamplerLoopOptions[]={"关闭","开启"};
inline constexpr int SamplerAttack=parameterID("soundnet.sampler","sampler.attack");
inline constexpr ParamInfo SamplerAttackInfo{"Attack",0.001f,5.0f,0.005f,"s"};
inline constexpr int SamplerRelease=parameterID("soundnet.sampler","sampler.release");
inline constexpr ParamInfo SamplerReleaseInfo{"Release",0.001f,5.0f,0.12f,"s"};
inline constexpr int SamplerStart=parameterID("soundnet.sampler","sampler.start");
inline constexpr ParamInfo SamplerStartInfo{"Start",0.0f,1.0f,0.0f,""};
inline constexpr int SamplerEnd=parameterID("soundnet.sampler","sampler.end");
inline constexpr ParamInfo SamplerEndInfo{"End",0.0f,1.0f,1.0f,""};
inline constexpr int SamplerReverse=parameterID("soundnet.sampler","sampler.reverse");
inline constexpr ParamInfo SamplerReverseInfo{"Reverse",0.0f,1.0f,0.0f,""};
inline constexpr const char* SamplerReverseOptions[]={"关闭","开启"};
inline constexpr int SamplerVelocity=parameterID("soundnet.sampler","sampler.velocity");
inline constexpr ParamInfo SamplerVelocityInfo{"Velocity",0.0f,1.0f,1.0f,""};
inline constexpr int SamplerXfade=parameterID("soundnet.sampler","sampler.loopXfade");
inline constexpr ParamInfo SamplerXfadeInfo{"Loop XFade",0.0f,100.0f,5.0f,"ms"};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{SamplerRoot,ParamSpec{&SamplerRootInfo,"sampler.root",false,nullptr,0,ParamGroup(1)}},{SamplerGain,ParamSpec{&SamplerGainInfo,"sampler.gain",false,nullptr,0,ParamGroup(6)}},{SamplerLoop,ParamSpec{&SamplerLoopInfo,"sampler.loop",true,SamplerLoopOptions,2,ParamGroup(3)}},{SamplerAttack,ParamSpec{&SamplerAttackInfo,"sampler.attack",false,nullptr,0,ParamGroup(2)}},{SamplerRelease,ParamSpec{&SamplerReleaseInfo,"sampler.release",false,nullptr,0,ParamGroup(2)}},{SamplerStart,ParamSpec{&SamplerStartInfo,"sampler.start",false,nullptr,0,ParamGroup(3)}},{SamplerEnd,ParamSpec{&SamplerEndInfo,"sampler.end",false,nullptr,0,ParamGroup(3)}},{SamplerReverse,ParamSpec{&SamplerReverseInfo,"sampler.reverse",true,SamplerReverseOptions,2,ParamGroup(3)}},{SamplerVelocity,ParamSpec{&SamplerVelocityInfo,"sampler.velocity",false,nullptr,0,ParamGroup(6)}},{SamplerXfade,ParamSpec{&SamplerXfadeInfo,"sampler.loopXfade",false,nullptr,0,ParamGroup(2)}}};}
}
