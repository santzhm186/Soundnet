#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::chord {
inline void appendTranslations(NodeModule& module) {
    const NodeTranslation rows[]={
        {"和弦输入","Chord Input"},
        {"输入","Input"},
        {"音符","Notes"},
        {"和弦播放","Chord Play"},
        {"关闭","Off"},
        {"开启","On"},
        {"和弦根音","Chord Root"},
        {"和弦性质","Chord Quality"},
        {"大三和弦","Major Triad"},
        {"小三和弦","Minor Triad"},
        {"属七","Dominant 7th"},
        {"大七","Major 7th"},
        {"小七","Minor 7th"},
        {"减三","Diminished Triad"},
        {"挂二","Suspended 2nd"},
        {"挂四","Suspended 4th"},
        {"时钟速度","Clock BPM"},
        {"和弦 / 走向，可手动输入","Chord / Progression"},
        {"预制 Cmaj7 / 爵士走向","Cmaj7 / Jazz Progression"},
        {"请输入和弦名称，如 Cmaj7 | Am7 | Dm7 | G7。","Enter chord names, such as Cmaj7 | Am7 | Dm7 | G7."},
    };
    for(const auto& row:rows) {
        bool exists=false;
        for(const auto& previous:module.translations)if(std::strcmp(previous.source,row.source)==0||std::strcmp(previous.en,row.en)==0){exists=true;break;}
        if(!exists)module.translations.push_back(row);
    }
}
}
