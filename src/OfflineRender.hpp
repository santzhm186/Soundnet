#pragma once
#include "Engine.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace tri::offline {
struct Result {
    std::vector<float> left,right;
    double rate=48000;
    double peak=0;
};

// Renders a graph without a host. Start nodes are pulsed once, DAW outputs are
// redirected to the hardware output when they are the graph's only output, and
// the result is a stereo float buffer ready for WAV writing.
inline bool render(GraphSpec graph,double seconds,double rate,Result& result,std::string& why) {
    if(!(seconds>.05)||seconds>120){why="导出时长需在 0.05–120 秒之间。";return false;}
    if(rate<8000||rate>192000){why="导出采样率需在 8–192 kHz 之间。";return false;}
    bool hasHardware=false,hasGraphOutput=false,hasDaw=false;
    for(const GraphNode& node:graph.nodes) {
        if(node.type==HardwareOutputNode)hasHardware=true;
        if(node.type==OutputNode)hasGraphOutput=true;
        if(node.type==DawOutputNode)hasDaw=true;
    }
    if(hasDaw&&!hasHardware&&!hasGraphOutput) {
        for(GraphNode& node:graph.nodes)if(node.type==DawOutputNode)node.type=HardwareOutputNode;
    }
    bool hasStart=false;
    for(GraphNode& node:graph.nodes) {
        if(node.type==StartNode) {node.parameters[TriggerPulse]=0;hasStart=true;}
    }
    Engine engine;
    if(!engine.setGraph(graph)){why="工作流未通过校验，无法导出。";return false;}
    engine.prepare(rate);
    if(hasStart) {
        for(GraphNode& node:graph.nodes)if(node.type==StartNode)node.parameters[TriggerPulse]=1;
        if(!engine.setGraph(graph)){why="工作流未通过校验，无法导出。";return false;}
    }
    size_t total=size_t(std::ceil(seconds*rate));
    if(total==0||total>size_t(120*192000)){why="导出时长超出范围。";return false;}
    result.rate=rate;
    result.left.assign(total,0);
    result.right.assign(total,0);
    constexpr int block=512;
    for(size_t offset=0;offset<total;offset+=block) {
        int count=int(std::min<size_t>(block,total-offset));
        engine.render(nullptr,nullptr,result.left.data()+offset,result.right.data()+offset,count);
    }
    for(size_t i=0;i<total;++i) {
        float l=result.left[i],r=result.right[i];
        if(!std::isfinite(l)||!std::isfinite(r)){why="渲染产生了非有限采样。";return false;}
        result.peak=std::max(result.peak,double(std::max(std::fabs(l),std::fabs(r))));
        result.left[i]=std::max(-1.f,std::min(1.f,l));
        result.right[i]=std::max(-1.f,std::min(1.f,r));
    }
    return true;
}
} // namespace tri::offline
