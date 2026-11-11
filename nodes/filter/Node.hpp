#pragma once
#include "../../platform/NodeDescriptor.hpp"
#include "Parameters.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.filter
// soundnet-order: 18
namespace tri::nodes::filter {
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.filter",typeID,"滤波器","声音处理",1,1,true,true,
             CapStream|CapBypassable,TimeMode::Stream,0,
             {pin("audio","音频",Signal::Audio,RoleMain,req)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {FilterType,FilterCutoff,FilterResonance,FilterDrive}, nullptr};
    spec.kernelFactory=create;
    spec.presets={
            {"低通 800 Hz",{{FilterType,0},{FilterCutoff,800},{FilterResonance,.7f},{FilterDrive,0}}},
            {"高通 180 Hz",{{FilterType,2},{FilterCutoff,180},{FilterResonance,.7f},{FilterDrive,0}}},
            {"带通 1.2 kHz",{{FilterType,1},{FilterCutoff,1200},{FilterResonance,1.2f},{FilterDrive,0}}} };
    spec.help="Type  0 低通  1 带通  2 高通\nCutoff 20–20000 Hz，Resonance 为 Q 值。";
    return {std::move(spec),parameterSpecs(),{{"音频","Audio"},{"处理","Processing"},{"低通","Low-pass"},{"高通","High-pass"},{"带通","Band-pass"},{"滤波器","Filter"},{"类型","Type"},{"截止频率","Cutoff"},{"共振","Resonance"},{"驱动","Drive"},{"低通 800 Hz","Low-pass 800 Hz"},{"高通 180 Hz","High-pass 180 Hz"},{"带通 1.2 kHz","Band-pass 1.2 kHz"},{"Type  0 低通  1 带通  2 高通\nCutoff 20–20000 Hz，Resonance 为 Q 值。","Type: 0 Low-pass · 1 Band-pass · 2 High-pass\nCutoff: 20–20000 Hz. Resonance sets Q."}}};
}
} // namespace tri::nodes::filter
