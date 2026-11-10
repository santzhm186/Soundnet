#pragma once
#include "Parameters.hpp"
#include "../../platform/runtime/MidiNoteState.hpp"
#include <algorithm>
namespace tri::nodes::scale_quantize {
inline int quantizePitch(int note,int root,int scale,int mode) noexcept {
    static constexpr uint16_t masks[]={0x0ab5,0x05ad,0x06ad,0x0295,0x0fff};
    const auto mask=masks[std::clamp(scale,0,4)];root=std::clamp(root,0,11);
    auto allowed=[&](int pitch){return pitch>=0&&pitch<128&&(mask&(1u<<((pitch-root+120)%12)));};
    if(allowed(note))return note;
    for(int d=1;d<=12;++d){if(mode!=1&&allowed(note-d))return note-d;if(mode!=2&&allowed(note+d))return note+d;}
    return note;
}
class Kernel final : public NodeKernel {
    MidiPitchMapper mapper;int root=0,scale=0,mode=0;bool dirty=true,lastBypass=false;
public:
    void prepare(double,int) override {mapper.reset();dirty=true;}
    void configure(const KernelConfig& c) override {
        root=std::clamp(int(c.value(QuantizeRoot,0)),0,11);scale=std::clamp(int(c.value(QuantizeScale,0)),0,4);
        mode=std::clamp(int(c.value(QuantizeMode,0)),0,2);dirty=true;
    }
    void reset(KernelResetReason) noexcept override {mapper.reset();dirty=true;}
    void process(KernelContext& c) noexcept override {
        const bool changed=dirty||lastBypass!=c.bypassed;dirty=false;lastBypass=c.bypassed;
        mapper.process(c,[&](int pitch,int){return c.bypassed?pitch:quantizePitch(pitch,root,scale,mode);},changed);
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
