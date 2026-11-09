// Transitional member implementation. New kernels use platform/NodeKernel.hpp.
    static void renderMetronome(const Entry& entry,Runtime& node,int n,double rate) {
        const double bpm=std::clamp(double(entry.metronomeBPM),40.0,240.0);
        const double step=bpm/(60.0*rate);
        const int beats=std::clamp(entry.metronomeBeats,1,16);
        const float level=std::clamp(entry.metronomeLevel,0.f,1.f);
        const float accent=std::clamp(entry.metronomeAccent,0.f,1.f);
        auto& state=*node.metronome;
        const float decay=std::exp(-1.f/float(0.035*rate));
        for(int i=0;i<n;++i) {
            state.envelope*=decay;
            state.clickPhase+=1800.0/rate;
            if(state.clickPhase>=1.0)state.clickPhase-=1.0;
            if(state.running) {
                state.phase+=step;
                if(state.phase>=1.0) {
                    state.phase-=1.0;
                    state.beat=(state.beat+1)%beats;
                    state.envelope=1;state.clickPhase=0;
                }
            }
            const float amp=state.envelope*level*(state.beat==0?accent:1.f);
            const float v=std::sin(float(2.0*3.14159265358979323846)*float(state.clickPhase))*amp;
            node.left[i]=node.right[i]=v;
        }
    }
