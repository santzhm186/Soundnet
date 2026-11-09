#pragma once
#include "../../platform/NodeKernel.hpp"
#include <array>
namespace tri::nodes::midi_split {
inline constexpr int splitID=parameterID("soundnet.midi-split","split");
class Kernel final : public NodeKernel {
    int split=60;
    // Remember the destination of each held note so changing the split point
    // cannot strand a note-on in a different output from its note-off.
    std::array<int8_t,2048> destinations{};
public:
    Kernel(){destinations.fill(-1);}
    void prepare(double,int) override {destinations.fill(-1);}
    void configure(const KernelConfig& config) override {split=int(config.value(splitID,60));}
    void reset(KernelResetReason) noexcept override {destinations.fill(-1);}
    void process(KernelContext& context) noexcept override {
        const auto* input=context.input("notes");
        auto* low=context.output("low");auto* high=context.output("high");
        if(!input)return;
        for(size_t i=0;i<input->midiCount;++i) {
            const auto event=input->midi[i];
            const int kind=event.status&0xf0;
            if(event.a>127||event.b>127)continue;
            if(kind==0x80||kind==0x90) {
                const int key=event.a*16+(event.status&15);
                const bool on=kind==0x90&&event.b;
                if(on&&destinations[key]>=0) {
                    auto* old=destinations[key]?high:low;
                    if(old)old->emitMidi({event.frame,uint8_t(0x80|(event.status&15)),event.a,0});
                }
                const int destination=on?(event.a>=split):
                    (destinations[key]>=0?destinations[key]:(event.a>=split));
                auto* out=destination?high:low;
                if(out&&out->emitMidi(event))destinations[key]=on?int8_t(destination):int8_t(-1);
            } else {
                if(low)low->emitMidi(event);
                if(high)high->emitMidi(event);
            }
        }
    }
};
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
