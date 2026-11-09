#pragma once
#include "../../platform/NodeKernel.hpp"
#include "../../platform/musicgen/PlaybackKernel.hpp"
namespace tri::nodes::yue2 {

inline constexpr int gainID =parameterID("soundnet.yue2","play.gain");
inline constexpr int loopID =parameterID("soundnet.yue2","play.loop");
inline constexpr int startID=parameterID("soundnet.yue2","play.start");
inline constexpr int endID  =parameterID("soundnet.yue2","play.end");
inline constexpr int fadeID =parameterID("soundnet.yue2","play.fade");

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
