#pragma once
#include "GraphSpec.hpp"
#include <cmath>
#include <string>

namespace tri {
// Session configuration is read before publishing a graph. It contains no
// tempo: musical speed belongs to the individual node that uses it.
struct ProjectEnvironment {
    bool present=false;
    double sampleRate=48000;
    int exportBits=24;
    int processingPrecision=32;
    std::string inputDevice="system",outputDevice="system";
    // Optional archive folder for generated material. Empty means "not set":
    // generation nodes then keep results embedded in the node only.
    std::string projectFolder;

    bool sameAudioConfiguration(const ProjectEnvironment& other) const {
        return present==other.present && sampleRate==other.sampleRate &&
            inputDevice==other.inputDevice && outputDevice==other.outputDevice;
    }
};

inline bool projectEnvironment(const GraphSpec& graph,ProjectEnvironment& result,std::string* why=nullptr) {
    ProjectEnvironment next;
    for(const auto& node:graph.nodes) {
        const std::string key=node.key.empty()?nodeKey(node.type):node.key;
        if(key!="soundnet.project-settings")continue;
        auto fail=[&](const char* reason){if(why)*why=reason;return false;};
        if(next.present)return fail("工程只能有一个项目设定节点。");
        if(node.unavailable)return fail("项目设定节点不可用，无法建立运行环境。");
        next.present=true;
        auto numeric=[&](const char* key,double fallback) {
            const int id=nodeParamIdForKey(node.type,key);
            if(id<0)return fallback;
            auto value=node.parameters.find(id);
            return value==node.parameters.end()?double(graphParamInfo(id).def):double(value->second);
        };
        next.sampleRate=numeric("project.sampleRate",48000);
        const double bits=numeric("project.exportBits",24);
        const double precision=numeric("project.processingPrecision",32);
        if(!std::isfinite(next.sampleRate)||next.sampleRate<8000||next.sampleRate>192000)
            return fail("项目采样率必须在 8–192 kHz 之间。");
        if(bits!=16 && bits!=24 && bits!=32)return fail("导出位深必须为 16、24 或 32。");
        if(precision!=32)return fail("当前音频处理接口使用 32 位浮点数。");
        next.exportBits=int(bits);next.processingPrecision=int(precision);
        auto text=[&](const char* key) {
            auto found=node.texts.find(key);
            return found==node.texts.end()||found->second.empty()?std::string("system"):found->second;
        };
        next.inputDevice=text("project.inputDevice");
        next.outputDevice=text("project.outputDevice");
        if(next.inputDevice.size()>1024||next.outputDevice.size()>1024)
            return fail("音频设备 UID 过长。");
        // A folder is optional; an absolute path is expected but not verified
        // here (the volume may not be mounted yet when the graph is read).
        auto folder=node.texts.find("project.folder");
        if(folder!=node.texts.end())next.projectFolder=folder->second;
        if(next.projectFolder.size()>4096)return fail("工程文件夹路径过长。");
    }
    result=std::move(next);
    return true;
}
}
