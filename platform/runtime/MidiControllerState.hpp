#pragma once
#include <array>
#include <cstdint>

namespace tri {
// Track only controls that can leave a voice latched or offset across transport
// changes. Patch, bank and continuous mix settings stay as sent to the receiver.
class MidiControllerState {
    static constexpr std::array<uint8_t,3> pedals{64,66,69};
    std::array<uint8_t,16> active{};
public:
    void observe(uint8_t status,uint8_t a,uint8_t b) noexcept {
        auto& mask=active[status&15];
        auto set=[&](int bit,bool on) {
            if(on)mask|=uint8_t(1u<<bit);
            else mask&=uint8_t(~(1u<<bit));
        };
        switch(status&0xf0) {
            case 0xb0:
                if(a==121)mask=0;
                else for(int i=0;i<int(pedals.size());++i)if(a==pedals[i])set(i,b>=64);
                break;
            case 0xe0: set(3,a!=0 || b!=64);break;
            case 0xd0: set(4,a!=0);break;
        }
    }
    template<typename Send> void release(Send send) noexcept {
        for(int channel=0;channel<16;++channel) {
            const uint8_t mask=active[channel];
            if(!mask)continue;
            for(int i=0;i<int(pedals.size());++i)if(mask&(1u<<i))
                send(uint8_t(0xb0|channel),pedals[i],uint8_t(0));
            if(mask&8)send(uint8_t(0xe0|channel),uint8_t(0),uint8_t(64));
            if(mask&16)send(uint8_t(0xd0|channel),uint8_t(0),uint8_t(0));
            active[channel]=0;
        }
    }
};
}
