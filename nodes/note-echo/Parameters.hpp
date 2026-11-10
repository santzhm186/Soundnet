#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::note_echo {
inline constexpr int typeID=nodeTypeID("soundnet.note-echo");
inline constexpr int EchoTime=parameterID("soundnet.note-echo","echo.time");
inline constexpr ParamInfo EchoTimeInfo{"Time",0.05f,2.0f,0.25f,"s"};
inline constexpr int EchoRepeats=parameterID("soundnet.note-echo","echo.repeats");
inline constexpr ParamInfo EchoRepeatsInfo{"Repeats",1.0f,8.0f,3.0f,""};
inline constexpr int EchoFeedback=parameterID("soundnet.note-echo","echo.feedback");
inline constexpr ParamInfo EchoFeedbackInfo{"Feedback",0.0f,1.0f,0.5f,""};
inline constexpr int EchoPitch=parameterID("soundnet.note-echo","echo.pitch");
inline constexpr ParamInfo EchoPitchInfo{"Pitch",-12.0f,12.0f,0.0f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{EchoTime,ParamSpec{&EchoTimeInfo,"echo.time",false,nullptr,0,ParamGroup(4)}},{EchoRepeats,ParamSpec{&EchoRepeatsInfo,"echo.repeats",true,nullptr,0,ParamGroup(4)}},{EchoFeedback,ParamSpec{&EchoFeedbackInfo,"echo.feedback",false,nullptr,0,ParamGroup(5)}},{EchoPitch,ParamSpec{&EchoPitchInfo,"echo.pitch",true,nullptr,0,ParamGroup(1)}}};}
}
