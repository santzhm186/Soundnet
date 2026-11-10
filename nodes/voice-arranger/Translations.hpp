#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::voice_arranger {
inline void appendTranslations(NodeModule& module) {
    const NodeTranslation rows[]={
        {"同一声部的最低音不能高于最高音。","A voice minimum cannot exceed its maximum."},
        {"三声部编排器","Three-Voice Arranger"},
        {"处理","Processing"},
        {"音符","Notes"},
        {"高声部","High Voice"},
        {"中声部","Middle Voice"},
        {"低声部","Low Voice"},
        {"高声部最低音","High minimum"},
        {"高声部最高音","High maximum"},
        {"高声部织体","High texture"},
        {"中声部最低音","Middle minimum"},
        {"中声部最高音","Middle maximum"},
        {"中声部织体","Middle texture"},
        {"低声部最低音","Low minimum"},
        {"低声部最高音","Low maximum"},
        {"低声部织体","Low texture"},
        {"最高音","Highest Note"},
        {"中间音","Middle Note"},
        {"最低音","Lowest Note"},
        {"柱式和弦（全部音）","Block Chord (All Notes)"},
        {"音域用 MIDI 音号表示（60 = C4）。选出的音按八度移入各声部音域。","Ranges use MIDI note numbers (60 = C4). Selected notes move by octaves into each voice's range."},
        {"织体","Texture"},
        {"音域需为 0–127 的整数，最低音不能高于最高音。","Use whole MIDI notes from 0 to 127. The minimum cannot exceed the maximum."},
    };
    for(const auto& row:rows) {
        bool exists=false;
        for(const auto& previous:module.translations)if(std::strcmp(previous.source,row.source)==0||std::strcmp(previous.en,row.en)==0){exists=true;break;}
        if(!exists)module.translations.push_back(row);
    }
}
}
