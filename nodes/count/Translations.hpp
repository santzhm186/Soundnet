#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::count {
inline void appendTranslations(NodeModule& module) {
    const NodeTranslation rows[]={
        {"次数条件","Event Counter"},
        {"逻辑","Logic"},
        {"触发","Trigger"},
        {"每收到 X 个信号后输出一次，再开始下一轮计数。可连接 Clip 的循环完成端口。","Emits one event for every X received, then resets the count. Connect a clip's Loop Complete output."},
        {"触发次数","Trigger Count"},
    };
    for(const auto& row:rows) {
        bool exists=false;
        for(const auto& previous:module.translations)if(std::strcmp(previous.source,row.source)==0||std::strcmp(previous.en,row.en)==0){exists=true;break;}
        if(!exists)module.translations.push_back(row);
    }
}
}
