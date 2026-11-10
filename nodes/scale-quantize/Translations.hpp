#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::scale_quantize {
inline void appendTranslations(NodeModule& module) {
    const NodeTranslation rows[]={
        {"音阶量化","Scale Quantizer"},
        {"处理","Processing"},
        {"音符","Notes"},
        {"C 大调","C Major"},
        {"A 小调","A Minor"},
        {"C 五声","C Pentatonic"},
        {"Root 调性（0=C），Scale 0 大调 1 小调 2 多利亚 3 五声 4 半音，Mode 0 最近 1 向上 2 向下。","Root: 0 = C. Scale: Major, Minor, Dorian, Pentatonic, Chromatic. Mode: Nearest, Up, Down."},
        {"根音","Root"},
        {"音阶","Scale"},
        {"大调","Major"},
        {"小调","Minor"},
        {"多利亚","Dorian"},
        {"五声","Pentatonic"},
        {"半音","Chromatic"},
        {"模式","Mode"},
        {"就近","Nearest"},
        {"向上","Up"},
        {"向下","Down"},
    };
    for(const auto& row:rows) {
        bool exists=false;
        for(const auto& previous:module.translations)if(std::strcmp(previous.source,row.source)==0||std::strcmp(previous.en,row.en)==0){exists=true;break;}
        if(!exists)module.translations.push_back(row);
    }
}
}
