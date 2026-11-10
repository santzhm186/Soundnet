#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::note_model {
inline void appendTranslations(NodeModule& module) {
    const NodeTranslation rows[]={
        {"音符模型","Note Model"},
        {"处理","Processing"},
        {"音符","Notes"},
    };
    for(const auto& row:rows) {
        bool exists=false;
        for(const auto& previous:module.translations)if(std::strcmp(previous.source,row.source)==0||std::strcmp(previous.en,row.en)==0){exists=true;break;}
        if(!exists)module.translations.push_back(row);
    }
}
}
