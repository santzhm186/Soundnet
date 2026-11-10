#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::audio_clip {
inline constexpr int typeID=nodeTypeID("soundnet.audio-clip");
inline constexpr int AudioClipGain=parameterID("soundnet.audio-clip","audioClip.gain");
inline constexpr ParamInfo AudioClipGainInfo{"Gain",0.0f,1.0f,0.9f,""};
inline constexpr int AudioClipLoop=parameterID("soundnet.audio-clip","audioClip.loop");
inline constexpr ParamInfo AudioClipLoopInfo{"Loop",0.0f,1.0f,0.0f,""};
inline constexpr const char* AudioClipLoopOptions[]={"关闭","开启"};
inline constexpr int AudioClipStart=parameterID("soundnet.audio-clip","audioClip.start");
inline constexpr ParamInfo AudioClipStartInfo{"Start",0.0f,1.0f,0.0f,""};
inline constexpr int AudioClipEnd=parameterID("soundnet.audio-clip","audioClip.end");
inline constexpr ParamInfo AudioClipEndInfo{"End",0.0f,1.0f,1.0f,""};
inline constexpr int AudioClipFade=parameterID("soundnet.audio-clip","audioClip.fade");
inline constexpr ParamInfo AudioClipFadeInfo{"Fade",0.0f,2.0f,0.01f,"s"};
inline constexpr int AudioClipPitch=parameterID("soundnet.audio-clip","audioClip.pitch");
inline constexpr ParamInfo AudioClipPitchInfo{"Pitch",-24.0f,24.0f,0.0f,"st"};
inline constexpr int AudioClipReverse=parameterID("soundnet.audio-clip","audioClip.reverse");
inline constexpr ParamInfo AudioClipReverseInfo{"Reverse",0.0f,1.0f,0.0f,""};
inline constexpr const char* AudioClipReverseOptions[]={"关闭","开启"};
inline constexpr int AudioClipXfade=parameterID("soundnet.audio-clip","audioClip.loopXfade");
inline constexpr ParamInfo AudioClipXfadeInfo{"Loop XFade",0.0f,100.0f,5.0f,"ms"};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{AudioClipGain,ParamSpec{&AudioClipGainInfo,"audioClip.gain",false,nullptr,0,ParamGroup(6)}},{AudioClipLoop,ParamSpec{&AudioClipLoopInfo,"audioClip.loop",true,AudioClipLoopOptions,2,ParamGroup(3)}},{AudioClipStart,ParamSpec{&AudioClipStartInfo,"audioClip.start",false,nullptr,0,ParamGroup(3)}},{AudioClipEnd,ParamSpec{&AudioClipEndInfo,"audioClip.end",false,nullptr,0,ParamGroup(3)}},{AudioClipFade,ParamSpec{&AudioClipFadeInfo,"audioClip.fade",false,nullptr,0,ParamGroup(2)}},{AudioClipPitch,ParamSpec{&AudioClipPitchInfo,"audioClip.pitch",false,nullptr,0,ParamGroup(1)}},{AudioClipReverse,ParamSpec{&AudioClipReverseInfo,"audioClip.reverse",true,AudioClipReverseOptions,2,ParamGroup(3)}},{AudioClipXfade,ParamSpec{&AudioClipXfadeInfo,"audioClip.loopXfade",false,nullptr,0,ParamGroup(2)}}};}
}
