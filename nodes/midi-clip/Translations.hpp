#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::midi_clip {
inline void appendTranslations(NodeModule& module) {
    const NodeTranslation rows[]={
        {"输入","Input"},
        {"触发","Trigger"},
        {"音符","Notes"},
        {"循环完成","Loop Complete"},
        {"乐谱（ABC）","Score (ABC)"},
        {"关闭","Off"},
        {"总长度（拍）","Length (beats)"},
        {"调制","Modulation"},
        {"延音","Sustain"},
        {"片段播放","Clip Play"},
        {"开启","On"},
        {"片段循环","Clip Loop"},
        {"时钟速度","Clock BPM"},
    };
    for(const auto& row:rows) {
        bool exists=false;
        for(const auto& previous:module.translations)if(std::strcmp(previous.source,row.source)==0||std::strcmp(previous.en,row.en)==0){exists=true;break;}
        if(!exists)module.translations.push_back(row);
    }
}
}
