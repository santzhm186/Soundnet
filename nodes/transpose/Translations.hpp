#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::transpose {
inline void appendTranslations(NodeModule& module) {
    const NodeTranslation rows[]={
        {"移调","Transpose"},
        {"处理","Processing"},
        {"音符","Notes"},
        {"升八度","Octave Up"},
        {"降八度","Octave Down"},
        {"把输入音符按半音移调，范围外自动夹到 0–127。旁通时保持原音高。","Transposes incoming notes in semitones, clamped to MIDI 0–127. Bypass preserves the original pitch."},
        {"移调半音","Semitones"},
    };
    for(const auto& row:rows) {
        bool exists=false;
        for(const auto& previous:module.translations)if(std::strcmp(previous.source,row.source)==0||std::strcmp(previous.en,row.en)==0){exists=true;break;}
        if(!exists)module.translations.push_back(row);
    }
}
}
