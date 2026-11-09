#pragma once
#include <cstdint>
#include <vector>

namespace tri {
// Musical time is measured in quarter notes, independent of the source file's tempo.
struct ClipEvent {
    double beat;
    uint8_t status, data1, data2;
};
struct MidiClip {
    std::vector<ClipEvent> events;
    double lengthBeats = 0;
};
} // namespace tri
