#pragma once
#include "GraphSpec.hpp"

namespace tri {
struct ArrangerVoice {
    int minimum=0, maximum=127;
    ArrangerTexture texture=ArrangerTexture::Chord;

    int fold(int pitch) const {
        if(pitch<minimum) pitch+=12*((minimum-pitch+11)/12);
        else if(pitch>maximum) pitch-=12*((pitch-maximum+11)/12);
        return pitch>=minimum && pitch<=maximum ? pitch : -1;
    }

    // Keys are ordered by incoming pitch, then MIDI channel. Rank the
    // playable input notes before folding so a bass line follows the bass.
    template<class Notes,class Emit>
    void voice(const Notes& held,const Notes& sounding,uint64_t previousToken,Emit emit) const {
        std::array<uint8_t,2048> velocity{};
        std::array<bool,2048> continuing{};
        std::array<int,2048> playable;
        int size=0;
        auto add=[&](int key,int pitch) {
            int out=pitch*16+key%16;
            velocity[out]=std::max(velocity[out],held[key].velocity);
            continuing[out]|=held[key].token<=previousToken;
        };
        for(int key=0;key<2048;++key) {
            if(!held[key].velocity)continue;
            int pitch=fold(key/16);
            if(pitch<0)continue;
            if(texture==ArrangerTexture::Chord)add(key,pitch);
            else playable[size++]=key;
        }
        if(size) {
            int index=texture==ArrangerTexture::Highest ? size-1
                      : texture==ArrangerTexture::Middle ? (size-1)/2 : 0;
            int key=playable[index];
            add(key,fold(key/16));
        }
        // A folded chord may contain several keys at the same output pitch.
        // Sustain it while any contributor remains, and articulate a fresh
        // attack when all previous contributors have been released.
        for(int key=0;key<2048;++key)
            if(sounding[key].velocity && (!velocity[key] || !continuing[key]))
                emit(uint8_t(0x80|key%16),uint8_t(key/16),uint8_t(0));
        for(int key=0;key<2048;++key)
            if(velocity[key] && (!sounding[key].velocity || !continuing[key]))
                emit(uint8_t(0x90|key%16),uint8_t(key/16),velocity[key]);
    }
};
} // namespace tri
