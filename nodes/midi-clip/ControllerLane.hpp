#pragma once
#include "../../platform/resources/MidiClip.hpp"
#include <algorithm>

namespace tri::nodes::midi_clip {
enum class Lane { Velocity, ControlChange, PitchBend, ChannelPressure, PolyPressure, ProgramChange };

struct ControllerLane {
    Lane kind=Lane::Velocity;
    int channel=0;
    int number=1;

    int type() const {
        switch(kind) {
            case Lane::Velocity: return 0x90;
            case Lane::ControlChange: return 0xb0;
            case Lane::PitchBend: return 0xe0;
            case Lane::ChannelPressure: return 0xd0;
            case Lane::PolyPressure: return 0xa0;
            case Lane::ProgramChange: return 0xc0;
        }
        return 0;
    }
    bool usesNumber() const {return kind==Lane::ControlChange || kind==Lane::PolyPressure;}
    int minimum() const {return kind==Lane::PitchBend?-8192:0;}
    int maximum() const {return kind==Lane::PitchBend?8191:127;}
    bool matches(const ClipEvent& event) const {
        return (event.status&0xf0)==type() && (event.status&15)==channel &&
            (!usesNumber() || event.data1==number);
    }
    int value(const ClipEvent& event) const {
        if(kind==Lane::PitchBend)return (event.data1 | (event.data2<<7))-8192;
        if(usesNumber())return event.data2;
        return event.data1;
    }
    ClipEvent event(double beat,int value) const {
        value=std::clamp(value,minimum(),maximum());
        const auto status=uint8_t(type()|channel);
        if(kind==Lane::PitchBend) {
            const int data=value+8192;
            return {beat,status,uint8_t(data&127),uint8_t(data>>7)};
        }
        if(usesNumber())return {beat,status,uint8_t(number),uint8_t(value)};
        return {beat,status,uint8_t(value),0};
    }
};
}
