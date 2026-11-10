#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::arp {
inline void appendTranslations(NodeModule& module) {
    const NodeTranslation rows[]={
        {"处理","Processing"},
        {"音符","Notes"},
        {"触发","Trigger"},
        {"Rhythm  0 直拍  1 Bossa Nova  2 Reggae  3 Disco  4 十六分","Rhythm: Straight, Bossa Nova, Reggae, Disco, 16th Notes."},
        {"琶音器","Arpeggiator"},
        {"关闭","Off"},
        {"开启","On"},
        {"节奏型","Rhythm"},
        {"直八","Straight 8ths"},
        {"十六分脉冲","Pulse 16ths"},
        {"琶音重复","Arp Repeat"},
        {"门限时值","Gate"},
        {"时钟速度","Clock BPM"},
    };
    for(const auto& row:rows) {
        bool exists=false;
        for(const auto& previous:module.translations)if(std::strcmp(previous.source,row.source)==0||std::strcmp(previous.en,row.en)==0){exists=true;break;}
        if(!exists)module.translations.push_back(row);
    }
}
}
