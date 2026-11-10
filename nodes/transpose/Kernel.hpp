#pragma once
#include "Parameters.hpp"
#include "../../platform/runtime/MidiNoteState.hpp"
#include <algorithm>
namespace tri::nodes::transpose {
class Kernel final : public NodeKernel {
    MidiPitchMapper mapper;int semitones=0; bool dirty=true,lastBypass=false;
public:
    void prepare(double,int) override {mapper.reset();dirty=true;}
    void configure(const KernelConfig& c) override {semitones=std::clamp(int(c.value(TransposeSemitones,0)),-24,24); dirty=true;}
    void reset(KernelResetReason) noexcept override {mapper.reset();dirty=true;}
    void process(KernelContext& c) noexcept override {
        const bool changed=dirty||lastBypass!=c.bypassed;dirty=false;lastBypass=c.bypassed;
        mapper.process(c,[&](int pitch,int){return std::clamp(pitch+(c.bypassed?0:semitones),0,127);},changed);
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
