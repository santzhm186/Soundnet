#pragma once
#include "AbcScore.hpp"
#include "../../platform/NodeKernel.hpp"
#include <array>

namespace tri::nodes::yue2 {

// Build the score once on the control thread. During rendering, the trigger
// starts the score at its own ABC tempo, independent of the audio region.
class ScorePlayback {
    MidiClip clip_;
    double secondsPerBeat_=0.5;
    uint64_t elapsed_=0;
    size_t next_=0;
    std::array<uint16_t,16*128> held_{};
    bool playing_=false;
    bool releasePending_=false;

    void rewind() noexcept {
        elapsed_=0;
        next_=0;
        playing_=false;
    }

    void onOverflow() noexcept {
        rewind();
        releasePending_=true;
    }

    bool release(KernelOutput& output,int frame) noexcept {
        releasePending_=false;
        for(size_t key=0;key<held_.size();++key) {
            if(!held_[key])continue;
            if(!output.emitMidi({frame,uint8_t(0x80|(key/128)),uint8_t(key%128),0})) {
                onOverflow();
                return false;
            }
            held_[key]=0;
        }
        return true;
    }

    bool advance(KernelOutput& output,double rate,int from,int to) noexcept {
        if(!playing_||to<=from)return true;
        const uint64_t end=elapsed_+uint64_t(to-from);
        for(;next_<clip_.events.size();++next_) {
            const auto& event=clip_.events[next_];
            const uint64_t due=uint64_t(std::llround(event.beat*secondsPerBeat_*rate));
            if(due>=end)break;
            const int frame=from+int(due>=elapsed_?due-elapsed_:0);
            auto& count=held_[size_t(event.status&15)*128+event.data1];
            const bool on=(event.status&0xf0)==0x90;
            // Multiple overlapping heads on one pitch share a MIDI gate.
            if((on&&count==0)||(!on&&count==1)) {
                if(!output.emitMidi({frame,event.status,event.data1,event.data2})) {
                    onOverflow();
                    return false;
                }
            }
            if(on)++count;
            else if(count)--count;
        }
        elapsed_=end;
        playing_=next_<clip_.events.size();
        return true;
    }

public:
    void configure(const std::string& text) {
        clip_={};
        reset();
        if(text.empty())return;
        abc::Score score;
        std::string why;
        if(!abc::parseClip(text,clip_,score,why))return;
        secondsPerBeat_=60.0/score.bpm;
        std::stable_sort(clip_.events.begin(),clip_.events.end(),[](const ClipEvent& a,const ClipEvent& b) {
            if(a.beat!=b.beat)return a.beat<b.beat;
            return (a.status&0xf0)<(b.status&0xf0);
        });
    }

    void reset() noexcept {
        // The graph clears its port and releases downstream notes on reset.
        rewind();
        held_.fill(0);
        releasePending_=false;
    }

    void process(KernelContext& context) noexcept {
        auto* output=context.output("notes");
        if(!output) {reset();return;}
        if(context.frames<=0||context.sampleRate<=0)return;
        if(releasePending_&&!release(*output,0))return;
        if(context.bypassed) {
            rewind();
            release(*output,0);
            return;
        }
        if(clip_.events.empty())return;
        int frame=0;
        if(const auto* input=context.input("trigger")) {
            for(size_t i=0;i<input->triggerCount;++i) {
                const auto& trigger=input->triggers[i];
                const int nextFrame=std::clamp(trigger.frame,frame,context.frames-1);
                if(!advance(*output,context.sampleRate,frame,nextFrame))return;
                if(!release(*output,nextFrame))return;
                rewind();
                playing_=trigger.start;
                frame=nextFrame;
            }
        }
        advance(*output,context.sampleRate,frame,context.frames);
    }
};
} // namespace tri::nodes::yue2
