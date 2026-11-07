#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace tri::smf {
// A small, dependency-free standard MIDI writer for the graph's live MIDI output.
// Times are seconds from the start of the export; the file uses 480 PPQ.
struct TimedEvent {
    double seconds;
    uint8_t status, key, value;
};

inline bool write(const std::string& path, const std::vector<TimedEvent>& input,
                  double duration, double bpm, std::string& why) {
    if (path.empty()) { why = "MIDI 输出路径为空。"; return false; }
    if (!(duration > 0) || duration > 120) { why = "MIDI 导出时长需在 0–120 秒之间。"; return false; }
    if (input.size() > 65535) { why = "MIDI 事件过多。"; return false; }
    if (!(bpm >= 20) || bpm > 400) { why = "MIDI 导出速度需在 20–400 BPM 之间。"; return false; }

    constexpr uint16_t ppq = 480;
    const double ticksPerSecond = ppq * bpm / 60.0;
    const auto endTick = uint64_t(std::llround(duration * ticksPerSecond));
    if (endTick > 0x0fffffffULL) { why = "MIDI 导出时长过长。"; return false; }

    std::vector<TimedEvent> events = input;
    std::stable_sort(events.begin(), events.end(), [](const TimedEvent& a, const TimedEvent& b) {
        return a.seconds < b.seconds;
    });

    std::vector<uint8_t> track;
    auto byte = [&](uint8_t v) { track.push_back(v); };
    auto u16 = [&](uint16_t v) { byte(uint8_t(v >> 8)); byte(uint8_t(v)); };
    auto u32 = [&](uint32_t v) { byte(uint8_t(v >> 24)); byte(uint8_t(v >> 16)); byte(uint8_t(v >> 8)); byte(uint8_t(v)); };
    auto vlq = [&](uint64_t value) {
        uint8_t bytes[10]; int n = 0;
        bytes[n++] = uint8_t(value & 127);
        while ((value >>= 7) != 0) bytes[n++] = uint8_t((value & 127) | 128);
        while (n) byte(bytes[--n]);
    };

    const uint32_t microseconds = uint32_t(std::llround(60000000.0 / bpm));
    vlq(0); byte(0xff); byte(0x51); byte(0x03);
    byte(uint8_t(microseconds >> 16)); byte(uint8_t(microseconds >> 8)); byte(uint8_t(microseconds));

    uint64_t previous = 0;
    for (const TimedEvent& e : events) {
        const uint8_t type = e.status & 0xf0;
        if (type < 0x80 || type > 0xe0 || e.key >= 128 || e.value >= 128) {
            why = "MIDI 事件包含无效的状态或数据字节。"; return false;
        }
        if (!std::isfinite(e.seconds)) { why = "MIDI 事件时间无效。"; return false; }
        uint64_t tick = uint64_t(std::llround(std::max(0.0, e.seconds) * ticksPerSecond));
        tick = std::min(tick, endTick);
        tick = std::max(tick, previous);
        vlq(tick - previous);
        previous = tick;
        byte(e.status);
        byte(e.key);
        if (type != 0xc0 && type != 0xd0) byte(e.value);
    }
    vlq(endTick - previous);
    byte(0xff); byte(0x2f); byte(0x00);
    if (track.size() > 0x0fffffffU) { why = "MIDI 轨道过大。"; return false; }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) { why = "无法写入 MIDI 文件。"; return false; }
    auto writeBytes = [&](const char* p, size_t n) { file.write(p, std::streamsize(n)); };
    writeBytes("MThd", 4);
    const uint8_t header[] = {0, 0, 0, 6, 0, 1, 0, 1, uint8_t(ppq >> 8), uint8_t(ppq)};
    writeBytes(reinterpret_cast<const char*>(header), sizeof(header));
    writeBytes("MTrk", 4);
    const uint8_t size[] = {uint8_t(track.size() >> 24), uint8_t(track.size() >> 16),
                            uint8_t(track.size() >> 8), uint8_t(track.size())};
    writeBytes(reinterpret_cast<const char*>(size), sizeof(size));
    writeBytes(reinterpret_cast<const char*>(track.data()), track.size());
    if (!file) { why = "写入 MIDI 文件失败。"; return false; }
    return true;
}
} // namespace tri::smf
