#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::channel_filter {
inline void appendTranslations(NodeModule& module) {
    const NodeTranslation rows[]={
        {"通道过滤","Channel Filter"},
        {"处理","Processing"},
        {"音符","Notes"},
        {"只放行指定 MIDI 通道（1–16）；0 表示全部通道，旁通时不过滤。","Passes only the selected MIDI channel (1–16). Channel 0 passes all; bypass disables filtering."},
        {"通道","Channel"},
    };
    for(const auto& row:rows) {
        bool exists=false;
        for(const auto& previous:module.translations)if(std::strcmp(previous.source,row.source)==0||std::strcmp(previous.en,row.en)==0){exists=true;break;}
        if(!exists)module.translations.push_back(row);
    }
}
}
