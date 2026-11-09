// Transitional member implementation. New kernels use platform/NodeKernel.hpp.
    static void renderChannelFilter(const Entry& entry,Runtime& node,Runtime* source,NotePort* input,int sourcePort) {
        (void)source;
        auto& state=*node.channelFilter;
        bool force=!input || state.channel!=entry.channelFilter || state.bypassed!=entry.bypass;
        if(force) {
            for(int k=0;k<2048;++k)if(node.after[k].velocity)
                node.emit({0,uint8_t(0x80|(k%16)),uint8_t(k/16),0});
            state.channel=entry.channelFilter;state.bypassed=entry.bypass;
        }
        if(!input)return;
        for(int j=0;j<input->eventCount;++j) {
            const auto event=input->events[j];
            if(entry.bypass || entry.channelFilter==0 || (event.status&15)==entry.channelFilter-1)
                node.emit(event);
        }
        node.controlSize=0;
    }
