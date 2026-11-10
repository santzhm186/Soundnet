#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::oscillator {
inline constexpr int typeID=nodeTypeID("soundnet.oscillator");
inline constexpr int Wave1=parameterID("soundnet.oscillator","osc1.wave");
inline constexpr ParamInfo Wave1Info{"Osc 1 Wave",0.0f,3.0f,2.0f,""};
inline constexpr const char* Wave1Options[]={"正弦","方波","锯齿","三角"};
inline constexpr int Level1=parameterID("soundnet.oscillator","osc1.level");
inline constexpr ParamInfo Level1Info{"Osc 1 Level",0.0f,1.0f,0.66f,""};
inline constexpr int Tune1=parameterID("soundnet.oscillator","osc1.tune");
inline constexpr ParamInfo Tune1Info{"Osc 1 Tune",-24.0f,24.0f,0.0f,"st"};
inline constexpr int Attack=parameterID("soundnet.oscillator","envelope.attack");
inline constexpr ParamInfo AttackInfo{"Attack",0.001f,3.0f,0.012f,"s"};
inline constexpr int Decay=parameterID("soundnet.oscillator","envelope.decay");
inline constexpr ParamInfo DecayInfo{"Decay",0.005f,3.0f,0.22f,"s"};
inline constexpr int Sustain=parameterID("soundnet.oscillator","envelope.sustain");
inline constexpr ParamInfo SustainInfo{"Sustain",0.0f,1.0f,0.7f,""};
inline constexpr int Release=parameterID("soundnet.oscillator","envelope.release");
inline constexpr ParamInfo ReleaseInfo{"Release",0.005f,5.0f,0.24f,"s"};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{Wave1,ParamSpec{&Wave1Info,"osc1.wave",true,Wave1Options,4,ParamGroup(1)}},{Level1,ParamSpec{&Level1Info,"osc1.level",false,nullptr,0,ParamGroup(6)}},{Tune1,ParamSpec{&Tune1Info,"osc1.tune",true,nullptr,0,ParamGroup(1)}},{Attack,ParamSpec{&AttackInfo,"envelope.attack",false,nullptr,0,ParamGroup(2)}},{Decay,ParamSpec{&DecayInfo,"envelope.decay",false,nullptr,0,ParamGroup(2)}},{Sustain,ParamSpec{&SustainInfo,"envelope.sustain",false,nullptr,0,ParamGroup(2)}},{Release,ParamSpec{&ReleaseInfo,"envelope.release",false,nullptr,0,ParamGroup(2)}}};}
}
