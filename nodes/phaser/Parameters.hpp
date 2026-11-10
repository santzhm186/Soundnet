#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::phaser {
inline constexpr int typeID=nodeTypeID("soundnet.phaser");
inline constexpr int PhaserRate=parameterID("soundnet.phaser","phaser.rate");
inline constexpr ParamInfo PhaserRateInfo{"Rate",0.05f,8.0f,0.5f,"Hz"};
inline constexpr int PhaserDepth=parameterID("soundnet.phaser","phaser.depth");
inline constexpr ParamInfo PhaserDepthInfo{"Depth",0.0f,1.0f,0.7f,""};
inline constexpr int PhaserFeedback=parameterID("soundnet.phaser","phaser.feedback");
inline constexpr ParamInfo PhaserFeedbackInfo{"Feedback",0.0f,0.9f,0.3f,""};
inline constexpr int PhaserMix=parameterID("soundnet.phaser","phaser.mix");
inline constexpr ParamInfo PhaserMixInfo{"Mix",0.0f,1.0f,0.5f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{PhaserRate,ParamSpec{&PhaserRateInfo,"phaser.rate",false,nullptr,0,ParamGroup(5)}},{PhaserDepth,ParamSpec{&PhaserDepthInfo,"phaser.depth",false,nullptr,0,ParamGroup(5)}},{PhaserFeedback,ParamSpec{&PhaserFeedbackInfo,"phaser.feedback",false,nullptr,0,ParamGroup(5)}},{PhaserMix,ParamSpec{&PhaserMixInfo,"phaser.mix",false,nullptr,0,ParamGroup(6)}}};}
}
