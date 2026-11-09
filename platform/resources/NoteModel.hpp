#pragma once
#include <array>
#include <cstdint>

namespace tri {
struct NoteModel {
    std::array<uint8_t,128> pitches{};
    NoteModel() {
        for (unsigned i=0; i<pitches.size(); ++i) pitches[i]=static_cast<uint8_t>(i);
    }
};
} // namespace tri
