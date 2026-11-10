#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::note_echo {
inline void appendTranslations(NodeModule& module) {
    const NodeTranslation rows[]={
        {"音符回声","Note Echo"},
        {"处理","Processing"},
        {"音符","Notes"},
        {"短回声","Short Echo"},
        {"八度回声","Octave Echo"},
        {"为输入音符安排多次延迟重复，每次可按 Pitch 移调并按 Feedback 衰减力度。原音立即通过。","Adds delayed repeats, transposing each by Pitch and reducing velocity by Feedback. The original note passes immediately."},
        {"时间","Time"},
        {"重复次数","Repeats"},
        {"反馈","Feedback"},
        {"音高","Pitch"},
    };
    for(const auto& row:rows) {
        bool exists=false;
        for(const auto& previous:module.translations)if(std::strcmp(previous.source,row.source)==0||std::strcmp(previous.en,row.en)==0){exists=true;break;}
        if(!exists)module.translations.push_back(row);
    }
}
}
