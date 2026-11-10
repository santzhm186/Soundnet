#pragma once
#include "../NodeKernel.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace tri {

// One render writer publishes an immutable prefix after each complete block.
// The control thread may stop and copy that prefix while a block is in flight.
class MidiInputCapture {
    struct TimedEvent {
        uint64_t frame=0;
        KernelMidiEvent event;
    };
    static constexpr size_t capacity=15000; // reserve room for paired releases
    static constexpr uint64_t frameMask=(uint64_t{1}<<48)-1;
    static constexpr uint64_t maxTicks=65535ull*midiClipTicksPerBeat;
    const double framesPerTick_;
    std::array<TimedEvent,capacity> events_{};
    size_t count_=0;
    uint64_t frames_=0;
    std::atomic<uint64_t> published_{0};
    std::atomic<bool> accepting_{true},overflow_{false};
public:
    MidiInputCapture(double rate,double tempo):framesPerTick_(rate*60.0/(tempo*midiClipTicksPerBeat)) {}
    void stop() noexcept {accepting_.store(false,std::memory_order_release);}
    bool overflowed() const noexcept {return overflow_.load(std::memory_order_acquire);}
    void overflow() noexcept {
        overflow_.store(true,std::memory_order_release);
        stop();
    }

    void append(const KernelMidiEvent* input,size_t count,int blockFrames) noexcept {
        if(!accepting_.load(std::memory_order_acquire))return;
        if(frames_+uint64_t(blockFrames)>uint64_t(framesPerTick_*double(maxTicks-1))) {
            overflow();return;
        }
        for(size_t i=0;i<count;++i) {
            const auto& event=input[i];
            if(event.status<0x80||event.status>=0xf0||event.a>127||event.b>127||
               event.frame<0||event.frame>=blockFrames)continue;
            if(count_==capacity) {
                overflow();return;
            }
            events_[count_++]={frames_+uint64_t(event.frame),event};
        }
        frames_+=uint64_t(blockFrames);
        published_.store((uint64_t(count_)<<48)|frames_,std::memory_order_release);
    }

    MidiClip clip() const {
        const uint64_t snapshot=published_.load(std::memory_order_acquire);
        const size_t count=size_t(snapshot>>48);
        const uint64_t end=uint64_t(std::llround(double(snapshot&frameMask)/framesPerTick_));
        uint64_t length=std::max(uint64_t(1),end);
        std::array<int64_t,16*128> held;
        held.fill(-1);
        MidiClip result;
        result.events.reserve(count);
        auto add=[&](uint64_t tick,uint8_t status,uint8_t a,uint8_t b) {
            length=std::max(length,tick);
            result.events.push_back({double(tick)/midiClipTicksPerBeat,status,a,b});
        };
        for(size_t i=0;i<count;++i) {
            const auto& entry=events_[i];
            const auto& event=entry.event;
            const int kind=event.status&0xf0,channel=event.status&15;
            uint64_t tick=uint64_t(std::llround(double(entry.frame)/framesPerTick_));
            auto& note=held[size_t(channel)*128+event.a];
            if(kind==0x90&&event.b) {
                if(note>=0) {
                    tick=std::max(tick,uint64_t(note)+1);
                    add(tick,uint8_t(0x80|channel),event.a,0);
                }
                note=int64_t(tick);
                add(tick,event.status,event.a,event.b);
            } else if(kind==0x80||kind==0x90) {
                if(note<0)continue;
                add(std::max(tick,uint64_t(note)+1),uint8_t(0x80|channel),event.a,event.b);
                note=-1;
            } else add(tick,event.status,event.a,event.b);
        }
        for(size_t key=0;key<held.size();++key)
            if(held[key]>=0)add(std::max(end,uint64_t(held[key])+1),
                                uint8_t(0x80|key/128),uint8_t(key%128),0);
        result.lengthBeats=double(length)/midiClipTicksPerBeat;
        return result;
    }
};
} // namespace tri
