#pragma once
#include "Parameters.hpp"
#include "../../platform/runtime/MidiNoteState.hpp"
#include <algorithm>
namespace tri::nodes::note_model {
class Kernel final : public NodeKernel {
    MidiPitchMapper mapper;std::shared_ptr<const NoteModel> model; bool dirty=true,lastBypass=false;
public:
    void prepare(double,int) override {mapper.reset();dirty=true;}
    void configure(const KernelConfig& c) override {model=c.model; dirty=true;}
    void reset(KernelResetReason) noexcept override {mapper.reset();dirty=true;}
    void process(KernelContext& c) noexcept override {
        const bool changed=dirty||lastBypass!=c.bypassed;dirty=false;lastBypass=c.bypassed;
        mapper.process(c,[&](int pitch,int){return c.bypassed||!model?pitch:int(model->pitches[size_t(pitch)]);},changed);
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
