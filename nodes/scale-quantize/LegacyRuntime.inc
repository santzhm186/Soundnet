// Transitional member implementation. New kernels use platform/NodeKernel.hpp.
    static int quantizePitch(int note,int root,int scale,int mode) {
        static constexpr uint16_t masks[5]={
            0x0ab5, // major: C D E F G A B
            0x05ad, // natural minor: C D Eb F G Ab Bb
            0x06ad, // dorian: C D Eb F G A Bb
            0x0295, // major pentatonic: C D E G A
            0x0fff  // chromatic
        };
        if(root<0)root=0;if(root>11)root=11;
        if(scale<0)scale=0;if(scale>4)scale=4;
        const uint16_t mask=masks[scale];
        auto inScale=[&](int pitch) {
            if(pitch<0||pitch>127)return false;
            int relative=(pitch-root)%12;if(relative<0)relative+=12;
            return ((mask>>relative)&1)!=0;
        };
        if(inScale(note))return note;
        if(mode==1) {
            for(int d=1;d<=12;++d)if(inScale(note+d))return note+d;
            return note;
        }
        if(mode==2) {
            for(int d=1;d<=12;++d)if(inScale(note-d))return note-d;
            return note;
        }
        for(int d=1;d<=12;++d) {
            if(inScale(note-d))return note-d;
            if(inScale(note+d))return note+d;
        }
        return note;
    }
    static void renderQuantize(const Entry& entry,Runtime& node,Runtime* source,NotePort* input,int sourcePort) {
        auto& state=*node.quantizer;
        const bool bypassed=entry.bypass;
        const int root=std::max(0,std::min(11,entry.quantizeRoot));
        const int scale=std::max(0,std::min(4,entry.quantizeScale));
        const int mode=std::max(0,std::min(2,entry.quantizeMode));
        bool force=source!=state.source || sourcePort!=state.sourcePort || !source;
        if(root!=state.root || scale!=state.scale || mode!=state.mode || bypassed!=state.bypassed)force=true;
        state.root=root;state.scale=scale;state.mode=mode;state.bypassed=bypassed;
        state.source=source;state.sourcePort=sourcePort;
        auto emitOff=[&](int key) {
            node.emit({0,uint8_t(0x80|(key%16)),uint8_t(key/16),0});
        };
        auto noteOn=[&](int pitch,int channel,uint8_t velocity,int frame) {
            const int inputKey=pitch*16+channel;
            const int old=state.mapping[inputKey];
            if(old>=0) {
                if(--state.counts[old]<=0) {state.counts[old]=0;emitOff(old);}
                state.mapping[inputKey]=-1;
            }
            const int outputPitch=bypassed?pitch:quantizePitch(pitch,root,scale,mode);
            const int outputKey=outputPitch*16+channel;
            if(state.counts[outputKey]==0)
                node.emit({frame,uint8_t(0x90|channel),uint8_t(outputPitch),velocity});
            ++state.counts[outputKey];
            state.mapping[inputKey]=outputKey;
        };
        auto syncHeld=[&]() {
            for(int k=0;k<2048;++k)if(node.after[k].velocity)emitOff(k);
            state.mapping.fill(-1);state.counts.fill(0);
            if(input)for(int k=0;k<2048;++k)
                if(input->before[k].velocity)noteOn(k/16,k%16,input->before[k].velocity,0);
        };
        if(force)syncHeld();
        if(input)for(int j=0;j<input->eventCount;++j) {
            const auto event=input->events[j];
            const uint8_t kind=event.status&0xf0;
            if(kind==0x90 && event.b)noteOn(event.a,event.status&15,event.b,event.frame);
            else if(kind==0x80 || kind==0x90) {
                // A note-off can close an older voice even if this pitch was
                // retriggered; the last release wins for that input key.
                const int outputKey=state.mapping[event.a*16+(event.status&15)];
                if(outputKey>=0) {
                    state.mapping[event.a*16+(event.status&15)]=-1;
                    if(--state.counts[outputKey]<=0) {
                        state.counts[outputKey]=0;
                        node.emit({event.frame,uint8_t(0x80|(outputKey%16)),uint8_t(outputKey/16),0});
                    }
                }
            } else node.emit({event.frame,event.status,event.a,event.b});
        }
        node.controlSize=0;
    }
