#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::random {
inline void appendTranslations(NodeModule& module) {
    const NodeTranslation rows[]={
        {"随机触发","Random Trigger"},
        {"逻辑","Logic"},
        {"触发","Trigger"},
        {"半数","Half"},
        {"稀疏","Sparse"},
        {"收到触发后按 Chance 概率决定是否继续转发，适合生成随机段落或概率门限。","Forwards incoming triggers with the selected probability. Useful for random sections and probability gates."},
        {"概率","Chance"},
    };
    for(const auto& row:rows) {
        bool exists=false;
        for(const auto& previous:module.translations)if(std::strcmp(previous.source,row.source)==0||std::strcmp(previous.en,row.en)==0){exists=true;break;}
        if(!exists)module.translations.push_back(row);
    }
}
}
