#pragma once
#include "../filter/Processor.hpp"
#include "../reverb/Processor.hpp"
#include "../noise/Processor.hpp"
#include "../compressor/Processor.hpp"
#include "../limiter/Processor.hpp"
#include "../chorus/Processor.hpp"
#include "../tremolo/Processor.hpp"
#include "../bitcrusher/Processor.hpp"
#include "../phaser/Processor.hpp"
#include "../noise-gate/Processor.hpp"
namespace tri {
inline std::unique_ptr<AudioProcessor> makeAudioProcessor(int type) {
    if(type==FilterNode)return std::make_unique<FilterProcessor>();
    if(type==ReverbNode)return std::make_unique<ReverbProcessor>();
    if(type==NoiseNode)return std::make_unique<NoiseProcessor>();
    if(type==CompressorNode)return std::make_unique<CompressorProcessor>();
    if(type==LimiterNode)return std::make_unique<LimiterProcessor>();
    if(type==GateNode)return std::make_unique<GateProcessor>();
    if(type==ChorusNode)return std::make_unique<ChorusProcessor>();
    if(type==TremoloNode)return std::make_unique<TremoloProcessor>();
    if(type==BitcrusherNode)return std::make_unique<BitcrusherProcessor>();
    if(type==PhaserNode)return std::make_unique<PhaserProcessor>();
    return nullptr;
}
}
