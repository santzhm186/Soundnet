#pragma once
#include "../../platform/NodeKernel.hpp"
#include "../../platform/musicgen/PlaybackKernel.hpp"
namespace tri::nodes::ace_step {

inline constexpr int gainID =parameterID("soundnet.ace-step","play.gain");
inline constexpr int loopID =parameterID("soundnet.ace-step","play.loop");
inline constexpr int startID=parameterID("soundnet.ace-step","play.start");
inline constexpr int endID  =parameterID("soundnet.ace-step","play.end");
inline constexpr int fadeID =parameterID("soundnet.ace-step","play.fade");
inline constexpr int secondsID=parameterID("soundnet.ace-step","gen.seconds");
inline constexpr int stepsID  =parameterID("soundnet.ace-step","gen.steps");
inline constexpr int seedID   =parameterID("soundnet.ace-step","gen.seed");

// Playback of the embedded generation result. Trigger start/stop controls
// playback; generation itself is a platform TaskService job (see Task.hpp).
class Kernel final : public NodeKernel {
    musicgen::PlaybackCore core{musicgen::PlaybackParamIDs{gainID,loopID,startID,endID,fadeID}};
public:
    void prepare(double sampleRate,int maxFrames) override {core.prepare(sampleRate,maxFrames);}
    void configure(const KernelConfig& config) override {core.configure(config,config.audio);}
    void process(KernelContext& context) noexcept override {core.process(context);}
    void reset(KernelResetReason) noexcept override {core.reset();}
};
inline std::unique_ptr<NodeKernel> create() {return std::make_unique<Kernel>();}
}
