#pragma once
// Shared playback core for generated-audio nodes.
//
// Both `soundnet.yue2` and `soundnet.ace-step` embed their active generation
// as an immutable AudioSample resource and play it through the graph, exactly
// like an audio node. The core is owned by each node's Kernel wrapper; nodes
// only supply their stable parameter IDs. Pure C++.
//
// Semantics (mirrors the historical audio-clip playback, reduced to the
// agreed parameter set — transposition/reversal belong to downstream nodes):
//   * A trigger start event at frame f starts playback at the configured
//     region start with an attack of `fade` seconds.
//   * A trigger stop event stops with a fade-out of the same length.
//   * reset() silences immediately (used for node stop, seek and panic).
//   * process() never allocates, locks or reads files; the sample handle is
//     acquired in configure() and only read afterwards.
#include "../../platform/NodeKernel.hpp"
#include <algorithm>
#include <cmath>

namespace tri::musicgen {

struct PlaybackParamIDs {
    int gain=0,loop=0,start=0,end=0,fade=0;
};

class PlaybackCore final {
public:
    explicit PlaybackCore(const PlaybackParamIDs& ids):ids_(ids) {}

    void prepare(double,double) {}

    void configure(const KernelConfig& config,const std::shared_ptr<const AudioSample>& sample) {
        sample_=sample;
        gain_=config.value(ids_.gain,1.f);
        loop_=config.value(ids_.loop,0.f)>0.5f;
        start_=std::clamp(config.value(ids_.start,0.f),0.f,1.f);
        end_=std::clamp(config.value(ids_.end,1.f),0.f,1.f);
        fade_=std::clamp(config.value(ids_.fade,0.05f),0.f,60.f);
        playing_=false;stopping_=false;pendingStart_=false;startFrame_=0;
        envelope_=0;position_=0;
    }

    void reset() noexcept {
        playing_=false;stopping_=false;pendingStart_=false;
        startFrame_=0;envelope_=0;position_=0;
    }

    void process(KernelContext& context) noexcept {
        auto* output=context.output("audio");
        if(output) {
            if(output->left)std::fill(output->left,output->left+context.frames,0.f);
            if(output->right)std::fill(output->right,output->right+context.frames,0.f);
        }
        if(context.bypassed) {reset();return;}
        // Consume trigger events: a start reschedules from the region start; a
        // stop fades out. Events are ordered by frame within the block.
        if(const auto* input=context.input("trigger")) {
            for(size_t i=0;i<input->triggerCount;++i) {
                const auto& event=input->triggers[i];
                if(event.start) {
                    pendingStart_=true;
                    startFrame_=std::clamp(event.frame,0,context.frames);
                    stopping_=false;
                } else {
                    pendingStart_=false;
                    if(playing_)stopping_=true;
                }
            }
        }
        const AudioSample* sample=sample_.get();
        if(!output||!sample||!sample->valid()) {reset();return;}
        const size_t sampleFrames=sample->frames();
        if(sampleFrames==0) {reset();return;}
        const int channels=sample->channels;
        const size_t startFrame=size_t(start_*double(sampleFrames>0?sampleFrames-1:0));
        size_t endFrame=size_t(end_*double(sampleFrames));
        if(endFrame<=startFrame)endFrame=std::min(sampleFrames,startFrame+1);
        int first=0;
        if(pendingStart_) {
            pendingStart_=false;
            position_=double(startFrame);
            envelope_=fade_>0?0.f:1.f;
            playing_=true;stopping_=false;
            first=std::max(0,std::min(startFrame_,context.frames));
            startFrame_=0;
        }
        if(!playing_)return;
        if(position_<double(startFrame)||position_>=double(endFrame))
            position_=double(startFrame);
        const bool loop=loop_&&endFrame>startFrame+1;
        const double rate=sample->rate>0?sample->rate:context.sampleRate;
        const double step=rate>0&&context.sampleRate>0?rate/context.sampleRate:1.0;
        const float fadeStep=fade_>0?float(1.0/(fade_*context.sampleRate)):1.f;
        for(int i=first;i<context.frames&&playing_;++i) {
            if(position_>=double(endFrame)) {
                if(loop)position_=double(startFrame);
                else if(stopping_&&envelope_>0)position_=std::max(double(startFrame),double(endFrame)-step);
                else {playing_=false;break;}
            }
            const size_t i0=std::min(size_t(position_),sampleFrames-1);
            size_t i1=i0+1;
            if(loop) {if(i1>=endFrame)i1=startFrame;}
            else i1=std::min(i1,sampleFrames-1);
            const float fraction=float(position_-double(i0));
            const float a=sample->samples[i0*size_t(channels)];
            const float b=sample->samples[i1*size_t(channels)];
            const float left=a+(b-a)*fraction;
            float right=left;
            if(channels>1) {
                const float ar=sample->samples[i0*size_t(channels)+1];
                const float br=sample->samples[i1*size_t(channels)+1];
                right=ar+(br-ar)*fraction;
            }
            const float amp=envelope_*gain_;
            if(output->left)output->left[i]=left*amp;
            if(output->right)output->right[i]=right*amp;
            if(stopping_) {
                envelope_-=fadeStep;
                if(envelope_<=0) {envelope_=0;playing_=false;break;}
            } else if(envelope_<1)envelope_=std::min(1.f,envelope_+fadeStep);
            position_+=step;
        }
    }

    bool playing() const noexcept {return playing_;}

private:
    PlaybackParamIDs ids_;
    std::shared_ptr<const AudioSample> sample_;
    double position_=0;
    float gain_=1,envelope_=0;
    bool loop_=false,playing_=false,stopping_=false,pendingStart_=false;
    int startFrame_=0;
    float start_=0,end_=1,fade_=0.05f;
};

} // namespace tri::musicgen
