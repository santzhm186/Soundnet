#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::voice_arranger {
inline constexpr int typeID=nodeTypeID("soundnet.voice-arranger");
inline constexpr int ArrHighMin=parameterID("soundnet.voice-arranger","high.minimum");
inline constexpr ParamInfo ArrHighMinInfo{"High minimum",0.0f,127.0f,72.0f,"MIDI"};
inline constexpr int ArrHighMax=parameterID("soundnet.voice-arranger","high.maximum");
inline constexpr ParamInfo ArrHighMaxInfo{"High maximum",0.0f,127.0f,95.0f,"MIDI"};
inline constexpr int ArrHighTexture=parameterID("soundnet.voice-arranger","high.texture");
inline constexpr ParamInfo ArrHighTextureInfo{"High texture",0.0f,3.0f,0.0f,""};
inline constexpr int ArrMidMin=parameterID("soundnet.voice-arranger","middle.minimum");
inline constexpr ParamInfo ArrMidMinInfo{"Middle minimum",0.0f,127.0f,55.0f,"MIDI"};
inline constexpr int ArrMidMax=parameterID("soundnet.voice-arranger","middle.maximum");
inline constexpr ParamInfo ArrMidMaxInfo{"Middle maximum",0.0f,127.0f,71.0f,"MIDI"};
inline constexpr int ArrMidTexture=parameterID("soundnet.voice-arranger","middle.texture");
inline constexpr ParamInfo ArrMidTextureInfo{"Middle texture",0.0f,3.0f,1.0f,""};
inline constexpr int ArrLowMin=parameterID("soundnet.voice-arranger","low.minimum");
inline constexpr ParamInfo ArrLowMinInfo{"Low minimum",0.0f,127.0f,36.0f,"MIDI"};
inline constexpr int ArrLowMax=parameterID("soundnet.voice-arranger","low.maximum");
inline constexpr ParamInfo ArrLowMaxInfo{"Low maximum",0.0f,127.0f,54.0f,"MIDI"};
inline constexpr int ArrLowTexture=parameterID("soundnet.voice-arranger","low.texture");
inline constexpr ParamInfo ArrLowTextureInfo{"Low texture",0.0f,3.0f,2.0f,""};
inline constexpr const char* textureOptions[]={"最高音","中间音","最低音","柱式和弦（全部音）"};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{ArrHighMin,ParamSpec{&ArrHighMinInfo,"high.minimum",true,nullptr,0,ParamGroup(8)}},{ArrHighMax,ParamSpec{&ArrHighMaxInfo,"high.maximum",true,nullptr,0,ParamGroup(8)}},{ArrHighTexture,ParamSpec{&ArrHighTextureInfo,"high.texture",true,textureOptions,4,ParamGroup(8)}},{ArrMidMin,ParamSpec{&ArrMidMinInfo,"middle.minimum",true,nullptr,0,ParamGroup(8)}},{ArrMidMax,ParamSpec{&ArrMidMaxInfo,"middle.maximum",true,nullptr,0,ParamGroup(8)}},{ArrMidTexture,ParamSpec{&ArrMidTextureInfo,"middle.texture",true,textureOptions,4,ParamGroup(8)}},{ArrLowMin,ParamSpec{&ArrLowMinInfo,"low.minimum",true,nullptr,0,ParamGroup(8)}},{ArrLowMax,ParamSpec{&ArrLowMaxInfo,"low.maximum",true,nullptr,0,ParamGroup(8)}},{ArrLowTexture,ParamSpec{&ArrLowTextureInfo,"low.texture",true,textureOptions,4,ParamGroup(8)}}};}
}
