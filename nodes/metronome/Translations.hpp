#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::metronome {
inline void appendTranslations(NodeModule& module) {
    const NodeTranslation rows[]={
        {"节拍器","Metronome"},
        {"逻辑","Logic"},
        {"触发","Trigger"},
        {"音频","Audio"},
        {"慢速","Slow"},
        {"快速","Fast"},
        {"开始触发后按 BPM 产生节拍，停止触发后静音。Beats 控制每小节拍数，Accent 控制第一拍重音。","Start triggers a click at the selected BPM; stop silences it. Beats sets the bar length; Accent emphasizes the first beat."},
        {"电平","Level"},
        {"重音","Accent"},
        {"拍数","Beats"},
    };
    for(const auto& row:rows) {
        bool exists=false;
        for(const auto& previous:module.translations)if(std::strcmp(previous.source,row.source)==0||std::strcmp(previous.en,row.en)==0){exists=true;break;}
        if(!exists)module.translations.push_back(row);
    }
}
}
