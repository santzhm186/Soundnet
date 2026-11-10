#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::start {
inline void appendTranslations(NodeModule& module) {
    const NodeTranslation rows[]={
        {"开始触发器","Start Trigger"},
        {"逻辑","Logic"},
        {"触发","Trigger"},
        {"触发脉冲","Trigger Pulse"},
    };
    for(const auto& row:rows) {
        bool exists=false;
        for(const auto& previous:module.translations)if(std::strcmp(previous.source,row.source)==0||std::strcmp(previous.en,row.en)==0){exists=true;break;}
        if(!exists)module.translations.push_back(row);
    }
}
}
