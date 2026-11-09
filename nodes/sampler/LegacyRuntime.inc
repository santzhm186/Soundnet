// Transitional member implementation. New kernels use platform/NodeKernel.hpp.
    static void samplerStop(SamplerState& state) {
        for(auto& voice:state.voices){voice.active=false;voice.releasing=false;voice.envelope=0;}
        state.sample=nullptr;
    }
    static void samplerNoteOn(const Entry& entry,SamplerState& state,int note,float velocity,double rate) {
        const AudioSample* sample=entry.sample.get();
        if(!sample || !sample->valid() || note<0 || note>127)return;
        int slot=-1;
        for(int i=0;i<int(state.voices.size());++i)if(!state.voices[i].active){slot=i;break;}
        if(slot<0) {
            slot=0;
            for(int i=1;i<int(state.voices.size());++i)
                if(state.voices[i].age<state.voices[slot].age)slot=i;
        }
        auto& voice=state.voices[slot];
        const size_t frames=sample->frames();
        double start=std::min(std::max(double(entry.samplerStart),0.0),1.0);
        double end=std::min(std::max(double(entry.samplerEnd),0.0),1.0);
        size_t startFrame=size_t(start*double(frames>0?frames-1:0));
        size_t endFrame=size_t(end*double(frames));
        if(endFrame<=startFrame)endFrame=std::min(frames,startFrame+1);
        voice.note=note;
        voice.direction=entry.samplerReverse?-1.0:1.0;
        voice.position=entry.samplerReverse?double(endFrame)-1.0:double(startFrame);
        voice.step=std::pow(2.0,(double(note)-double(entry.samplerRoot))/12.0)*sample->rate/rate;
        const float sensitivity=std::min(std::max(entry.samplerVelocity,0.f),1.f);
        voice.velocity=(1.0f-sensitivity)+sensitivity*std::min(std::max(velocity,0.f),1.f);
        voice.envelope=entry.samplerAttack>0 ? 0.f : 1.f;
        voice.attackStep=entry.samplerAttack>0 ? 1.f/float(entry.samplerAttack*rate) : 1.f;
        voice.releaseStep=entry.samplerRelease>0 ? 1.f/float(entry.samplerRelease*rate) : 1.f;
        voice.age=++state.serial;
        voice.active=true;voice.releasing=false;
    }
    static void renderSampler(const Entry& entry,Runtime* source,NotePort* input,int frames,bool force,double rate,int sourcePort) {
        auto& node=*entry.state;
        auto& state=*node.sampler;
        const AudioSample* sample=entry.sample.get();
        if(!sample || !sample->valid()) {
            if(state.sample)samplerStop(state);
            return;
        }
        const uint64_t generation=source?source->generation:0;
        const bool reconnected=state.generation!=generation || state.sourcePort!=sourcePort;
        bool sync=force || reconnected || state.sample!=sample;
        if(sync) {
            samplerStop(state);
            state.generation=generation;state.sourcePort=sourcePort;
            state.sample=sample;state.serial=0;
            if(input)for(int k=0;k<2048;++k)
                if(input->before[k].velocity)
                    samplerNoteOn(entry,state,k/16,float(input->before[k].velocity)/127.f,rate);
        }
        const size_t sampleFrames=sample->frames();
        const int channels=sample->channels;
        double start=std::min(std::max(double(entry.samplerStart),0.0),1.0);
        double end=std::min(std::max(double(entry.samplerEnd),0.0),1.0);
        size_t startFrame=size_t(start*double(sampleFrames>0?sampleFrames-1:0));
        size_t endFrame=size_t(end*double(sampleFrames));
        if(endFrame<=startFrame)endFrame=std::min(sampleFrames,startFrame+1);
        const bool loop=entry.samplerLoop && endFrame>startFrame+1;
        const double xfade=loop ? std::min(double(endFrame-startFrame)*.5,double(entry.samplerXfade)*rate) : 0.0;
        const float outputGain=entry.samplerGain;
        int position=0;
        auto renderTo=[&](int limit) {
            limit=std::max(0,std::min(limit,frames));
            for(;position<limit;++position) {
                for(auto& voice:state.voices) {
                    if(!voice.active)continue;
                    if(voice.direction>0 && voice.position>=double(sampleFrames)){voice.active=false;continue;}
                    if(voice.direction<0 && voice.position<double(startFrame)){voice.active=false;continue;}
                    const size_t i0=size_t(voice.position);
                    size_t i1=i0+1;
                    if(loop) {
                        if(i1>=endFrame)i1=startFrame;
                    } else i1=std::min(i1,sampleFrames-1);
                    const float fraction=float(voice.position-double(i0));
                    auto sampleAt=[&](int channel) {
                        const float a=sample->samples[i0*size_t(channels)+size_t(channel)];
                        const float b=sample->samples[i1*size_t(channels)+size_t(channel)];
                        return a+(b-a)*fraction;
                    };
                    float left=sampleAt(0),right=channels>1?sampleAt(1):left;
                    float loopGain=1.f;
                    if(xfade>0.0) {
                        const double sinceStart=voice.direction>0 ? voice.position-double(startFrame) : double(endFrame)-voice.position;
                        const double remaining=voice.direction>0 ? double(endFrame)-voice.position : voice.position-double(startFrame);
                        if(sinceStart<xfade)loopGain*=float(std::max(0.0,sinceStart)/xfade);
                        if(remaining<xfade)loopGain*=float(std::max(0.0,remaining)/xfade);
                    }
                    const float amp=voice.envelope*loopGain*voice.velocity*outputGain;
                    node.left[position]+=left*amp;
                    node.right[position]+=right*amp;
                    if(voice.releasing) {
                        voice.envelope-=voice.releaseStep;
                        if(voice.envelope<=0){voice.envelope=0;voice.active=false;continue;}
                    } else if(voice.envelope<1) {
                        voice.envelope=std::min(1.f,voice.envelope+voice.attackStep);
                    }
                    voice.position+=voice.step*voice.direction;
                    if(voice.direction>0 && voice.position>=double(endFrame)) {
                        if(loop) {
                            const double length=double(endFrame-startFrame);
                            voice.position=double(startFrame)+std::fmod(voice.position-double(startFrame),length);
                        } else voice.active=false;
                    } else if(voice.direction<0 && voice.position<double(startFrame)) {
                        if(loop) {
                            const double length=double(endFrame-startFrame);
                            voice.position=double(endFrame)-1.0-std::fmod(double(startFrame)-voice.position,length);
                            if(voice.position<double(startFrame))voice.position=double(endFrame)-1.0;
                        } else voice.active=false;
                    }
                }
            }
        };
        if(sync && (!input || !input->eventCount || input->events[0].frame>0))renderTo(0);
        if(input) {
            int cursor=0;
            while(cursor<input->eventCount) {
                const int frame=std::max(0,std::min(input->events[cursor].frame,frames));
                renderTo(frame);
                do {
                    const auto event=input->events[cursor++];
                    const int kind=event.status&0xf0;
                    if(kind==0x80 || (kind==0x90 && !event.b)) {
                        for(auto& voice:state.voices)
                            if(voice.active && !voice.releasing && voice.note==event.a)
                                voice.releasing=true;
                    } else if(kind==0x90) {
                        samplerNoteOn(entry,state,event.a,float(event.b)/127.f,rate);
                    }
                } while(cursor<input->eventCount && input->events[cursor].frame==frame);
            }
        }
        renderTo(frames);
    }
