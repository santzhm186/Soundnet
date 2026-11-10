#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::velocity {
inline void appendTranslations(NodeModule& module) {
    const NodeTranslation rows[]={
        {"力度自动化","Velocity Automation"},
        {"处理","Processing"},
        {"音符","Notes"},
        {"最小力度","Velocity Minimum"},
        {"最大力度","Velocity Maximum"},
        {"力度周期拍数","Velocity Beats"},
        {"力度曲线","Velocity Shape"},
        {"时钟速度","Clock BPM"},
    };
    for(const auto& row:rows) {
        bool exists=false;
        for(const auto& previous:module.translations)if(std::strcmp(previous.source,row.source)==0||std::strcmp(previous.en,row.en)==0){exists=true;break;}
        if(!exists)module.translations.push_back(row);
    }
}
}
