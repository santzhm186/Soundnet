#pragma once
#include "MidiClip.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace tri::smf {
constexpr size_t maxFileBytes = 4 * 1024 * 1024;
constexpr size_t maxEvents = 32768;
constexpr unsigned maxBeats = 65536;

class Reader {
    const uint8_t *next;
    size_t remaining;
public:
    Reader(const uint8_t *bytes, size_t size) : next(bytes), remaining(size) {}
    size_t size() const { return remaining; }
    const uint8_t *take(size_t n) {
        if (n > remaining) throw std::runtime_error("MIDI 文件已截断。");
        const auto *begin = next;
        next += n;
        remaining -= n;
        return begin;
    }
    uint8_t byte() { return *take(1); }
    uint16_t u16() { auto p=take(2); return uint16_t(p[0]) << 8 | p[1]; }
    uint32_t u32() {
        auto p=take(4);
        return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    }
    uint32_t vlq() {
        uint32_t value=0;
        for (int i=0; i<4; ++i) {
            uint8_t b=byte();
            value=(value<<7) | (b&127);
            if (!(b&128)) return value;
        }
        throw std::runtime_error("MIDI 可变长度字段无效。");
    }
};

struct TickEvent {
    uint64_t tick;
    uint8_t status, key, value;
};

inline uint64_t readTrack(Reader track, uint16_t ppq, std::vector<TickEvent>& events) {
    uint64_t tick=0;
    uint8_t running=0;
    while (track.size()) {
        tick += track.vlq();
        if (tick > uint64_t(maxBeats)*ppq) throw std::runtime_error("Clip 超过 65536 拍。");
        uint8_t first=track.byte();
        if (first==0xff || first==0xf0 || first==0xf7) {
            running=0;
            bool end=first==0xff && track.byte()==0x2f;
            auto length=track.vlq();
            track.take(length);
            if (end) {
                if (length || track.size()) throw std::runtime_error("MIDI 轨道结束标记无效。");
                return tick;
            }
            continue;
        }
        if (first>=0xf0 || (first<0x80 && !running)) throw std::runtime_error("MIDI 状态字节无效。");
        if (first>=0x80) running=first;
        uint8_t type=running&0xf0;
        uint8_t a=first>=0x80 ? track.byte() : first;
        uint8_t b=(type==0xc0 || type==0xd0) ? 0 : track.byte();
        if (a>=128 || b>=128) throw std::runtime_error("MIDI 数据字节无效。");
        bool pedalOrPanic=type==0xb0 && (a==64 || a==120 || a==121 || a==123);
        if (type==0x80 || type==0x90 || pedalOrPanic) {
            if (events.size()>=maxEvents) throw std::runtime_error("Clip 超过 32768 个音符或踏板事件。");
            events.push_back({tick,running,a,b});
        }
    }
    throw std::runtime_error("MIDI 轨道缺少结束标记。");
}

// Merge simultaneous tracks in their original order. Resolve sustain into note
// durations here, so a clip's pedal cannot sustain a different live MIDI source.
inline MidiClip parse(const uint8_t *bytes, size_t size) {
    if (!size || size>maxFileBytes) throw std::runtime_error("MIDI 文件大小应在 1 字节到 4 MB 之间。");
    Reader file(bytes,size);
    if (std::memcmp(file.take(4),"MThd",4)) throw std::runtime_error("请选择标准 MIDI 文件（.mid 或 .midi）。");
    auto headerSize=file.u32();
    Reader header(file.take(headerSize),headerSize);
    auto format=header.u16(), tracks=header.u16(), ppq=header.u16();
    if (format>1 || !tracks || (format==0 && tracks!=1)) throw std::runtime_error("支持同步的标准 MIDI 格式 0 和 1。");
    if (!ppq || ppq&0x8000) throw std::runtime_error("Clip 需要使用拍号分辨率 PPQ，请从 DAW 按音乐拍导出 MIDI。");
    std::vector<TickEvent> events;
    uint64_t endTick=0;
    for (int i=0; i<tracks;) {
        bool isTrack=!std::memcmp(file.take(4),"MTrk",4);
        auto n=file.u32();
        Reader chunk(file.take(n),n);
        if (!isTrack) continue;
        endTick=std::max(endTick,readTrack(chunk,ppq,events));
        ++i;
    }
    if (!endTick) throw std::runtime_error("Clip 长度为零。");
    std::stable_sort(events.begin(),events.end(),[](const auto& a,const auto& b){return a.tick<b.tick;});
    MidiClip clip;
    clip.lengthBeats=double(endTick)/ppq;
    struct Key { unsigned held=0; bool sounding=false; };
    std::array<std::array<Key,128>,16> keys{};
    std::array<bool,16> pedal{};
    auto emit=[&](uint64_t tick,int status,int key,int value) {
        if (clip.events.size()>=maxEvents) throw std::runtime_error("展开踏板后超过 32768 个音符事件。");
        clip.events.push_back({double(tick)/ppq,uint8_t(status),uint8_t(key),uint8_t(value)});
    };
    for (auto e:events) {
        const int channel=e.status&15, type=e.status&0xf0;
        Key& k=keys[channel][e.key];
        if (type==0x90 && e.value) {
            if (k.sounding) emit(e.tick,0x80|channel,e.key,0);
            ++k.held;
            k.sounding=true;
            emit(e.tick,e.status,e.key,e.value);
        } else if (type==0x80 || type==0x90) {
            if (k.held) --k.held;
            if (k.sounding && !k.held && !pedal[channel]) {
                k.sounding=false;
                emit(e.tick,0x80|channel,e.key,e.value);
            }
        } else {
            if (e.key==64) pedal[channel]=e.value>=64;
            if (e.key==121) pedal[channel]=false;
            for (int key=0; key<128; ++key) {
                Key& held=keys[channel][key];
                if (e.key==120 || e.key==123) held.held=0;
                if (held.sounding && !held.held && (!pedal[channel] || e.key==120)) {
                    held.sounding=false;
                    emit(e.tick,0x80|channel,key,0);
                }
            }
        }
    }
    if (clip.events.empty()) throw std::runtime_error("这个 MIDI 文件没有可演奏的音符。");
    for (int ch=0; ch<16; ++ch)
        for (int key=0; key<128; ++key)
            if (keys[ch][key].sounding) emit(endTick,0x80|ch,key,0);
    return clip;
}
} // namespace tri::smf
