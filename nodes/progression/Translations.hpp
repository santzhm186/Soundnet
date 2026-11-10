#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::progression {
inline void appendTranslations(NodeModule& module) {
    const NodeTranslation rows[]={
        {"和声走向","Chord Progression"},
        {"输入","Input"},
        {"触发","Trigger"},
        {"音符","Notes"},
        {"走向播放","Progression Play"},
        {"关闭","Off"},
        {"开启","On"},
        {"走向长度","Progression Length"},
        {"每和弦小节数","Bars Per Chord"},
        {"走向根音 1","Progress Root 1"},
        {"走向性质 1","Progress Quality 1"},
        {"走向根音 2","Progress Root 2"},
        {"走向性质 2","Progress Quality 2"},
        {"走向根音 3","Progress Root 3"},
        {"走向性质 3","Progress Quality 3"},
        {"走向根音 4","Progress Root 4"},
        {"走向性质 4","Progress Quality 4"},
        {"走向根音 5","Progress Root 5"},
        {"走向性质 5","Progress Quality 5"},
        {"走向根音 6","Progress Root 6"},
        {"走向性质 6","Progress Quality 6"},
        {"走向根音 7","Progress Root 7"},
        {"走向性质 7","Progress Quality 7"},
        {"走向根音 8","Progress Root 8"},
        {"走向性质 8","Progress Quality 8"},
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
