#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::midi_output {
inline constexpr int typeID=nodeTypeID("soundnet.midi-output");
inline constexpr int MidiOutChannel=parameterID("soundnet.midi-output","midiOut.channel");
inline constexpr ParamInfo MidiOutChannelInfo{"Channel",0.0f,16.0f,0.0f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{MidiOutChannel,ParamSpec{&MidiOutChannelInfo,"midiOut.channel",true,nullptr,0,ParamGroup(0)}}};}
}
