#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::comment {
inline void appendTranslations(NodeModule& module) {
    const NodeTranslation rows[]={
        {"注释","Comment"},
        {"注释节点不参与音频或 MIDI；用上方名称字段记录说明文字。","Comments do not process audio or MIDI. Use the name field above to add notes."},
    };
    for(const auto& row:rows) {
        bool exists=false;
        for(const auto& previous:module.translations)if(std::strcmp(previous.source,row.source)==0||std::strcmp(previous.en,row.en)==0){exists=true;break;}
        if(!exists)module.translations.push_back(row);
    }
}
}
