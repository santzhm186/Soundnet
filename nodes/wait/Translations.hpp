#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::wait {
inline void appendTranslations(NodeModule& module) {
    const NodeTranslation rows[]={
        {"等待","Wait"},
        {"逻辑","Logic"},
        {"触发","Trigger"},
        {"收到开始或停止信号后，等待指定秒数再转发。可排队等待多个信号。","Delays incoming start or stop events by the specified number of seconds. Multiple events can be queued."},
        {"等待时间","Wait Seconds"},
    };
    for(const auto& row:rows) {
        bool exists=false;
        for(const auto& previous:module.translations)if(std::strcmp(previous.source,row.source)==0||std::strcmp(previous.en,row.en)==0){exists=true;break;}
        if(!exists)module.translations.push_back(row);
    }
}
}
