// Transitional member implementation. New kernels use platform/NodeKernel.hpp.
    static void renderArranger(const Entry& entry,Runtime* source,NotePort* input,bool planChanged,int sourcePort) {
        auto& node=*entry.state;
        auto& state=*node.arranger;
        uint64_t generation=source ? source->generation : 0;
        bool reconnected=state.sourceGeneration!=generation || state.sourcePort!=sourcePort;
        state.sourceGeneration=generation;state.sourcePort=sourcePort;
        if(reconnected)state.previousToken=0;
        uint64_t token=input ? input->beforeSerial : 0;
        bool sync=planChanged || state.retry || reconnected;
        if(sync)node.incoming=input ? input->before : Notes{};
        auto revoice=[&](int frame) {
            for(int v=0;v<3;++v) {
                auto& port=node.notePort(v);
                entry.voices[v].voice(node.incoming,port.after,state.previousToken,[&](uint8_t s,uint8_t k,uint8_t vel) {
                    port.emit({frame,s,k,vel});
                });
            }
            state.previousToken=token;
        };
        if(sync && (!input || !input->eventCount || input->events[0].frame>0))revoice(0);
        int cursor=0;
        while(input && cursor<input->eventCount) {
            int frame=input->events[cursor].frame;
            do {
                auto event=input->events[cursor++];
                int kind=event.status&0xf0,key=event.a*16+(event.status&15);
                if(kind==0x90 && event.b)node.incoming[key]={event.b,++token};
                else if(kind==0x80 || kind==0x90)node.incoming[key]={};
            } while(cursor<input->eventCount && input->events[cursor].frame==frame);
            revoice(frame);
        }
        state.retry=node.overflow || state.otherVoices[0].overflow || state.otherVoices[1].overflow;
    }
