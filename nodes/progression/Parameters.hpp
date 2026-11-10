#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::progression {
inline constexpr int typeID=nodeTypeID("soundnet.progression");
inline constexpr int ProgressRun=parameterID("soundnet.progression","progression.run");
inline constexpr ParamInfo ProgressRunInfo{"Progression Play",0.0f,1.0f,0.0f,""};
inline constexpr const char* ProgressRunOptions[]={"关闭","开启"};
inline constexpr int ProgressLength=parameterID("soundnet.progression","progression.length");
inline constexpr ParamInfo ProgressLengthInfo{"Progression Length",1.0f,8.0f,4.0f,""};
inline constexpr int ProgressBars=parameterID("soundnet.progression","progression.bars");
inline constexpr ParamInfo ProgressBarsInfo{"Bars Per Chord",1.0f,8.0f,1.0f,""};
inline constexpr int ProgressRoot1=parameterID("soundnet.progression","progression.root.1");
inline constexpr ParamInfo ProgressRoot1Info{"Progress Root 1",24.0f,96.0f,60.0f,""};
inline constexpr int ProgressQuality1=parameterID("soundnet.progression","progression.quality.1");
inline constexpr ParamInfo ProgressQuality1Info{"Progress Quality 1",0.0f,7.0f,3.0f,""};
inline constexpr int ProgressRoot2=parameterID("soundnet.progression","progression.root.2");
inline constexpr ParamInfo ProgressRoot2Info{"Progress Root 2",24.0f,96.0f,69.0f,""};
inline constexpr int ProgressQuality2=parameterID("soundnet.progression","progression.quality.2");
inline constexpr ParamInfo ProgressQuality2Info{"Progress Quality 2",0.0f,7.0f,4.0f,""};
inline constexpr int ProgressRoot3=parameterID("soundnet.progression","progression.root.3");
inline constexpr ParamInfo ProgressRoot3Info{"Progress Root 3",24.0f,96.0f,62.0f,""};
inline constexpr int ProgressQuality3=parameterID("soundnet.progression","progression.quality.3");
inline constexpr ParamInfo ProgressQuality3Info{"Progress Quality 3",0.0f,7.0f,4.0f,""};
inline constexpr int ProgressRoot4=parameterID("soundnet.progression","progression.root.4");
inline constexpr ParamInfo ProgressRoot4Info{"Progress Root 4",24.0f,96.0f,67.0f,""};
inline constexpr int ProgressQuality4=parameterID("soundnet.progression","progression.quality.4");
inline constexpr ParamInfo ProgressQuality4Info{"Progress Quality 4",0.0f,7.0f,2.0f,""};
inline constexpr int ProgressRoot5=parameterID("soundnet.progression","progression.root.5");
inline constexpr ParamInfo ProgressRoot5Info{"Progress Root 5",24.0f,96.0f,60.0f,""};
inline constexpr int ProgressQuality5=parameterID("soundnet.progression","progression.quality.5");
inline constexpr ParamInfo ProgressQuality5Info{"Progress Quality 5",0.0f,7.0f,0.0f,""};
inline constexpr int ProgressRoot6=parameterID("soundnet.progression","progression.root.6");
inline constexpr ParamInfo ProgressRoot6Info{"Progress Root 6",24.0f,96.0f,60.0f,""};
inline constexpr int ProgressQuality6=parameterID("soundnet.progression","progression.quality.6");
inline constexpr ParamInfo ProgressQuality6Info{"Progress Quality 6",0.0f,7.0f,0.0f,""};
inline constexpr int ProgressRoot7=parameterID("soundnet.progression","progression.root.7");
inline constexpr ParamInfo ProgressRoot7Info{"Progress Root 7",24.0f,96.0f,60.0f,""};
inline constexpr int ProgressQuality7=parameterID("soundnet.progression","progression.quality.7");
inline constexpr ParamInfo ProgressQuality7Info{"Progress Quality 7",0.0f,7.0f,0.0f,""};
inline constexpr int ProgressRoot8=parameterID("soundnet.progression","progression.root.8");
inline constexpr ParamInfo ProgressRoot8Info{"Progress Root 8",24.0f,96.0f,60.0f,""};
inline constexpr int ProgressQuality8=parameterID("soundnet.progression","progression.quality.8");
inline constexpr ParamInfo ProgressQuality8Info{"Progress Quality 8",0.0f,7.0f,0.0f,""};
inline constexpr int ClockBPM=parameterID("soundnet.progression","clock.bpm");
inline constexpr ParamInfo ClockBPMInfo{"Clock BPM",40.0f,240.0f,112.0f,"BPM"};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{ProgressRun,ParamSpec{&ProgressRunInfo,"progression.run",true,ProgressRunOptions,2,ParamGroup(3)}},{ProgressLength,ParamSpec{&ProgressLengthInfo,"progression.length",true,nullptr,0,ParamGroup(4)}},{ProgressBars,ParamSpec{&ProgressBarsInfo,"progression.bars",true,nullptr,0,ParamGroup(4)}},{ProgressRoot1,ParamSpec{&ProgressRoot1Info,"progression.root.1",true,nullptr,0,ParamGroup(8)}},{ProgressQuality1,ParamSpec{&ProgressQuality1Info,"progression.quality.1",true,nullptr,0,ParamGroup(8)}},{ProgressRoot2,ParamSpec{&ProgressRoot2Info,"progression.root.2",true,nullptr,0,ParamGroup(8)}},{ProgressQuality2,ParamSpec{&ProgressQuality2Info,"progression.quality.2",true,nullptr,0,ParamGroup(8)}},{ProgressRoot3,ParamSpec{&ProgressRoot3Info,"progression.root.3",true,nullptr,0,ParamGroup(8)}},{ProgressQuality3,ParamSpec{&ProgressQuality3Info,"progression.quality.3",true,nullptr,0,ParamGroup(8)}},{ProgressRoot4,ParamSpec{&ProgressRoot4Info,"progression.root.4",true,nullptr,0,ParamGroup(8)}},{ProgressQuality4,ParamSpec{&ProgressQuality4Info,"progression.quality.4",true,nullptr,0,ParamGroup(8)}},{ProgressRoot5,ParamSpec{&ProgressRoot5Info,"progression.root.5",true,nullptr,0,ParamGroup(8)}},{ProgressQuality5,ParamSpec{&ProgressQuality5Info,"progression.quality.5",true,nullptr,0,ParamGroup(8)}},{ProgressRoot6,ParamSpec{&ProgressRoot6Info,"progression.root.6",true,nullptr,0,ParamGroup(8)}},{ProgressQuality6,ParamSpec{&ProgressQuality6Info,"progression.quality.6",true,nullptr,0,ParamGroup(8)}},{ProgressRoot7,ParamSpec{&ProgressRoot7Info,"progression.root.7",true,nullptr,0,ParamGroup(8)}},{ProgressQuality7,ParamSpec{&ProgressQuality7Info,"progression.quality.7",true,nullptr,0,ParamGroup(8)}},{ProgressRoot8,ParamSpec{&ProgressRoot8Info,"progression.root.8",true,nullptr,0,ParamGroup(8)}},{ProgressQuality8,ParamSpec{&ProgressQuality8Info,"progression.quality.8",true,nullptr,0,ParamGroup(8)}},{ClockBPM,ParamSpec{&ClockBPMInfo,"clock.bpm",false,nullptr,0,ParamGroup(4)}}};}
}
