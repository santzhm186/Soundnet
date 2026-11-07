#pragma once
#include "Engine.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace tri::offline {
struct MidiMessage {
    double seconds;
    uint8_t status,a,b;
};
struct Result {
    std::vector<float> left,right;
    std::vector<MidiMessage> midi;
    double rate=48000;
    double peak=0;
    double bpm=120;
};
struct Stem {
    InstanceID id=0;
    int type=0;
    std::string name;
    Result result;
};

struct MidiCapture {
    Result* result=nullptr;
    double rate=48000;
    size_t offset=0;
};
inline void captureMidi(void* ptr,int frame,uint8_t status,uint8_t a,uint8_t b) {
    auto* capture=static_cast<MidiCapture*>(ptr);
    if(!capture || !capture->result || frame<0) return;
    capture->result->midi.push_back({double(capture->offset+size_t(frame))/capture->rate,status,a,b});
}

// Renders a graph without a host. Start nodes are pulsed once, DAW outputs are
// redirected to the hardware output when they are the graph's only output, and
// the result is a stereo float buffer ready for WAV writing.
inline bool render(GraphSpec graph,double seconds,double rate,Result& result,std::string& why) {
    if(!(seconds>.05)||seconds>120){why="导出时长需在 0.05–120 秒之间。";return false;}
    if(rate<8000||rate>192000){why="导出采样率需在 8–192 kHz 之间。";return false;}
    bool hasHardware=false,hasGraphOutput=false,hasDaw=false;
    double bpm=120;
    for(const GraphNode& node:graph.nodes) {
        if(node.type==ClockNode) bpm=graphParamValue(node,ClockBPM);
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
    MidiCapture capture{&result,rate,0};
    engine.setMidiOutput(&capture,captureMidi);
    if(hasStart) {
        for(GraphNode& node:graph.nodes)if(node.type==StartNode)node.parameters[TriggerPulse]=1;
        if(!engine.setGraph(graph)){why="工作流未通过校验，无法导出。";return false;}
    }
    size_t total=size_t(std::ceil(seconds*rate));
    if(total==0||total>size_t(120*192000)){why="导出时长超出范围。";return false;}
    result.rate=rate;
    result.bpm=bpm;
    result.left.assign(total,0);
    result.right.assign(total,0);
    result.midi.clear();
    constexpr int block=512;
    for(size_t offset=0;offset<total;offset+=block) {
        int count=int(std::min<size_t>(block,total-offset));
        capture.offset=offset;
        engine.render(nullptr,nullptr,result.left.data()+offset,result.right.data()+offset,count);
    }
    std::stable_sort(result.midi.begin(),result.midi.end(),[](const MidiMessage& a,const MidiMessage& b){
        return a.seconds<b.seconds;
    });
    for(size_t i=0;i<total;++i) {
        float l=result.left[i],r=result.right[i];
        if(!std::isfinite(l)||!std::isfinite(r)){why="渲染产生了非有限采样。";return false;}
        result.peak=std::max(result.peak,double(std::max(std::fabs(l),std::fabs(r))));
        result.left[i]=std::max(-1.f,std::min(1.f,l));
        result.right[i]=std::max(-1.f,std::min(1.f,r));
    }
    return true;
}
// Renders each output node separately, muting the others, so a project can be
// exported as a set of stems for mixing. DAW outputs are treated as hardware
// outputs for the isolated pass, matching render()'s offline behavior.
inline bool renderStems(GraphSpec graph,double seconds,double rate,
        std::vector<Stem>& stems,std::string& why) {
    stems.clear();
    std::vector<size_t> outputs;
    for(size_t i=0;i<graph.nodes.size();++i) {
        int type=graph.nodes[i].type;
        if(type==HardwareOutputNode||type==DawOutputNode||type==OutputNode)
            outputs.push_back(i);
    }
    if(outputs.empty()) {why="工作流没有音频输出节点。";return false;}
    if(outputs.size()>64) {why="音频输出节点过多。";return false;}
    for(size_t index:outputs) {
        GraphSpec copy=graph;
        for(size_t i=0;i<copy.nodes.size();++i) {
            GraphNode& node=copy.nodes[i];
            const bool output=node.type==HardwareOutputNode||node.type==DawOutputNode||node.type==OutputNode;
            if(i==index) {
                if(node.type==DawOutputNode) node.type=HardwareOutputNode;
            } else if(output) {
                node.bypass=true;
            }
        }
        Result result;
        if(!render(copy,seconds,rate,result,why)) return false;
        const GraphNode& original=graph.nodes[index];
        Stem stem;
        stem.id=original.id;
        stem.type=original.type;
        stem.name=std::string(nodeTitle(original.type))+" #"+std::to_string(original.id);
        stem.result=std::move(result);
        stems.push_back(std::move(stem));
    }
    return true;
}
} // namespace tri::offline
