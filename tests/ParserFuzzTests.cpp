#include "../src/AudioFile.hpp"
#include "../src/StandardMidiFile.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

int main() {
    std::mt19937 rng(0x5eed1234);
    std::string why;
    int parsedAudio = 0, parsedMidi = 0;
    for (int iteration = 0; iteration < 4000; ++iteration) {
        std::vector<uint8_t> bytes(rng() % 384);
        for (uint8_t& byte : bytes) byte = uint8_t(rng());
        if (bytes.size() >= 12 && iteration % 4 == 0) {
            std::memcpy(bytes.data(), "RIFF", 4);
            std::memcpy(bytes.data() + 8, "WAVE", 4);
        } else if (bytes.size() >= 12 && iteration % 4 == 1) {
            std::memcpy(bytes.data(), "FORM", 4);
            std::memcpy(bytes.data() + 8, iteration % 2 ? "AIFF" : "AIFC", 4);
        } else if (bytes.size() >= 14 && iteration % 4 == 2) {
            std::memcpy(bytes.data(), "MThd", 4);
            bytes[4] = 0; bytes[5] = 0; bytes[6] = 0; bytes[7] = 6;
            bytes[8] = 0; bytes[9] = 1; bytes[10] = 0; bytes[11] = 1; bytes[12] = 1; bytes[13] = 0xe0;
        }
        tri::AudioSample sample;
        if (tri::audiofile::parse(bytes.data(), bytes.size(), sample, why)) ++parsedAudio;
        try {
            auto clip = tri::smf::parse(bytes.data(), bytes.size());
            (void)clip;
            ++parsedMidi;
        } catch (...) {
        }
    }
    std::printf("PASS: 4000 random payloads rejected without crashing (audio=%d midi=%d)\n", parsedAudio, parsedMidi);
    return 0;
}
