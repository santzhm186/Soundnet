// Transitional member implementation. New kernels use platform/NodeKernel.hpp.
    static void renderEcho(const Entry& entry,Runtime& node,Runtime* source,NotePort* input,uint64_t samplePosition,double rate,int n,int sourcePort) {
        auto& state=*node.echo;
        const float time=std::clamp(entry.echoTime,.05f,2.f);
        const int repeats=std::clamp(entry.echoRepeats,1,8);
        const float feedback=std::clamp(entry.echoFeedback,0.f,1.f);
        const int pitch=std::clamp(entry.echoPitch,-12,12);
        bool force=source!=state.source || sourcePort!=state.sourcePort || !source;
        if(time!=state.time || feedback!=state.feedback || repeats!=state.repeats || pitch!=state.pitch || entry.bypass!=state.bypassed)force=true;
        if(force) {
            for(int k=0;k<2048;++k)if(node.after[k].velocity)node.emit({0,uint8_t(0x80|(k%16)),uint8_t(k/16),0});
            state.count=0;
            state.source=source;state.sourcePort=sourcePort;
            state.time=time;state.feedback=feedback;state.repeats=repeats;state.pitch=pitch;state.bypassed=entry.bypass;
            if(entry.bypass && input)for(int k=0;k<2048;++k)if(input->before[k].velocity)
                node.emit({0,uint8_t(0x90|(k%16)),uint8_t(k/16),input->before[k].velocity});
        }
        if(entry.bypass) {
            if(input)for(int j=0;j<input->eventCount;++j)node.emit({input->events[j].frame,input->events[j].status,input->events[j].a,input->events[j].b});
            return;
        }
        auto schedule=[&](uint64_t due,uint8_t status,uint8_t a,uint8_t b,int step) {
            if(state.count>=int(state.queue.size())){node.overflow=true;return;}
            state.queue[state.count++]={due,status,a,b,step};
        };
        auto processDue=[&](uint64_t limit) {
            for(int i=0;i<state.count;) {
                if(state.queue[i].due>=limit){++i;continue;}
                EchoEvent e=state.queue[i];
                state.queue[i]=state.queue[--state.count];
                const int frame=e.due<=samplePosition?0:int(e.due-samplePosition);
                node.emit({frame,e.status,e.a,e.b});
                if((e.status&0xf0)==0x90 && e.b) {
                    schedule(e.due+uint64_t(.15*rate),uint8_t(0x80|(e.status&15)),e.a,0,e.step);
                    if(e.step<state.repeats) {
                        const int nextPitch=std::clamp(int(e.a)+state.pitch,0,127);
                        const uint8_t nextVelocity=uint8_t(std::clamp(int(std::lround(e.b*state.feedback)),1,127));
                        schedule(e.due+uint64_t(state.time*rate),uint8_t(0x90|(e.status&15)),uint8_t(nextPitch),nextVelocity,e.step+1);
                    }
                }
            }
        };
        processDue(samplePosition+uint64_t(n));
        if(input)for(int j=0;j<input->eventCount;++j) {
            const auto event=input->events[j];
            node.emit({event.frame,event.status,event.a,event.b});
            const uint8_t kind=event.status&0xf0;
            if(kind==0x90 && event.b) {
                const int nextPitch=std::clamp(int(event.a)+state.pitch,0,127);
                const uint8_t nextVelocity=uint8_t(std::clamp(int(std::lround(event.b*state.feedback)),1,127));
                schedule(samplePosition+uint64_t(event.frame)+uint64_t(state.time*rate),uint8_t(0x90|(event.status&15)),uint8_t(nextPitch),nextVelocity,1);
            }
        }
    }
