// Transitional member implementation. New kernels use platform/NodeKernel.hpp.
    static void renderAudioClip(const Entry& entry,Runtime& node,int frames,double rate) {
        auto& state=*node.audioClip;
        const AudioSample* sample=entry.sample.get();
        if(!sample || !sample->valid()) {state=AudioClipState{};return;}
        if(state.sample!=sample) {
            const bool pending=state.pendingStart;
            const int startFrame=state.startFrame;
            state=AudioClipState{};
            state.sample=sample;
            state.pendingStart=pending;
            state.startFrame=startFrame;
        }
        const size_t sampleFrames=sample->frames();
        const int channels=sample->channels;
        const double start=std::min(std::max(double(entry.audioClipStart),0.0),1.0);
        const double end=std::min(std::max(double(entry.audioClipEnd),0.0),1.0);
        const size_t startFrame=size_t(start*double(sampleFrames>0?sampleFrames-1:0));
        size_t endFrame=size_t(end*double(sampleFrames));
        if(endFrame<=startFrame)endFrame=std::min(sampleFrames,startFrame+1);
        const bool reverse=entry.audioClipReverse;
        const double direction=reverse?-1.0:1.0;
        const double step=double(sample->rate)/rate*std::pow(2.0,double(entry.audioClipPitch)/12.0);
        int first=0;
        if(state.pendingStart) {
            state.pendingStart=false;
            state.position=reverse?double(endFrame)-1.0:double(startFrame);
            state.envelope=entry.audioClipFade>0 ? 0.f : 1.f;
            state.playing=true;state.stopping=false;
            first=std::max(0,std::min(state.startFrame,frames));
            state.startFrame=0;
        }
        if(!state.playing)return;
        if(state.position<double(startFrame) || state.position>=double(endFrame))
            state.position=reverse?double(endFrame)-1.0:double(startFrame);
        const bool loop=entry.audioClipLoop && endFrame>startFrame+1;
        const double xfade=loop ? std::min(double(endFrame-startFrame)*.5,double(entry.audioClipXfade)*rate) : 0.0;
        const float fadeStep=entry.audioClipFade>0 ? 1.f/float(entry.audioClipFade*rate) : 1.f;
        for(int i=first;i<frames && state.playing;++i) {
            if(direction>0 && state.position>=double(endFrame)) {
                if(loop)state.position=double(startFrame);
                else if(state.stopping && state.envelope>0)state.position=std::max(double(startFrame),double(endFrame)-step);
                else {state.playing=false;break;}
            }
            if(direction<0 && state.position<double(startFrame)) {
                if(loop)state.position=std::max(double(startFrame),double(endFrame)-step);
                else if(state.stopping && state.envelope>0)state.position=double(startFrame);
                else {state.playing=false;break;}
            }
            const size_t i0=std::min(size_t(state.position),sampleFrames-1);
            size_t i1=i0+1;
            if(loop) {if(i1>=endFrame)i1=startFrame;}
            else i1=std::min(i1,sampleFrames-1);
            const float fraction=float(state.position-double(i0));
            const float a=sample->samples[i0*size_t(channels)];
            const float b=sample->samples[i1*size_t(channels)];
            const float left=a+(b-a)*fraction;
            float right=left;
            if(channels>1) {
                const float ar=sample->samples[i0*size_t(channels)+1];
                const float br=sample->samples[i1*size_t(channels)+1];
                right=ar+(br-ar)*fraction;
            }
            float loopGain=1.f;
            if(xfade>0.0) {
                const double sinceStart=direction>0 ? state.position-double(startFrame) : double(endFrame)-state.position;
                const double remaining=direction>0 ? double(endFrame)-state.position : state.position-double(startFrame);
                if(sinceStart<xfade)loopGain*=float(std::max(0.0,sinceStart)/xfade);
                if(remaining<xfade)loopGain*=float(std::max(0.0,remaining)/xfade);
            }
            const float amp=state.envelope*loopGain*entry.audioClipGain;
            node.left[i]+=left*amp;node.right[i]+=right*amp;
            if(state.stopping) {
                state.envelope-=fadeStep;
                if(state.envelope<=0){state.envelope=0;state.playing=false;break;}
            } else if(state.envelope<1)state.envelope=std::min(1.f,state.envelope+fadeStep);
            state.position+=step*direction;
            if(!loop && ((direction>0 && state.position>=double(endFrame)) ||
                         (direction<0 && state.position<double(startFrame)))) {
                state.stopping=true;
                if(entry.audioClipFade<=0){state.playing=false;break;}
            }
        }
    }
