// Transitional member implementation. New kernels use platform/NodeKernel.hpp.
    static void renderTranspose(const Entry& entry,Runtime& node,Runtime* source,NotePort* input,int sourcePort) {
        auto& state=*node.transpose;
        const int semitones=std::clamp(entry.transposeSemitones,-24,24);
        const bool bypassed=entry.bypass;
        const int shift=bypassed?0:semitones;
        bool force=source!=state.source || sourcePort!=state.sourcePort || !source;
        if(semitones!=state.semitones || bypassed!=state.bypassed)force=true;
        state.source=source;state.sourcePort=sourcePort;state.semitones=semitones;state.bypassed=bypassed;
        auto emitOff=[&](int key,int frame){node.emit({frame,uint8_t(0x80|(key%16)),uint8_t(key/16),0});};
        auto noteOn=[&](int key,uint8_t velocity,int frame) {
            const int channel=key%16;
            const int pitch=std::clamp(key/16+shift,0,127);
            const int outputKey=pitch*16+channel;
            if(!node.after[outputKey].velocity || state.mapping[key]==outputKey)
                node.emit({frame,uint8_t(0x90|channel),uint8_t(pitch),velocity});
            state.mapping[key]=outputKey;
        };
        auto noteOff=[&](int key,int frame) {
            const int outputKey=state.mapping[key];
            if(outputKey<0)return;
            state.mapping[key]=-1;
            const int pitch=outputKey/16,channel=outputKey%16;
            // Only a clamped endpoint can have multiple inputs on this channel.
            if((pitch==0&&shift<0) || (pitch==127&&shift>0)) {
                const int first=pitch==0?0:127-shift;
                const int last=pitch==0?-shift:127;
                for(int inputPitch=first;inputPitch<=last;++inputPitch)
                    if(state.mapping[inputPitch*16+channel]==outputKey)return;
            }
            emitOff(outputKey,frame);
        };
        auto syncHeld=[&](){
            for(int k=0;k<2048;++k)if(node.after[k].velocity)emitOff(k,0);
            state.mapping.fill(-1);
            if(input)for(int k=0;k<2048;++k)
                if(input->before[k].velocity)noteOn(k,input->before[k].velocity,0);
        };
        if(force)syncHeld();
        if(input)for(int j=0;j<input->eventCount;++j){
            const auto event=input->events[j];const uint8_t kind=event.status&0xf0;
            const int key=event.a*16+(event.status&15);
            if(kind==0x90 && event.b)noteOn(key,event.b,event.frame);
            else if(kind==0x80 || kind==0x90)noteOff(key,event.frame);
            else node.emit(event);
        }
    }
