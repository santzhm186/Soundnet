#pragma once
#include <AudioToolbox/AudioToolbox.h>
#include "../app/PatchRouting.hpp"

namespace ternaryAU {
// The layout uses one private property per coordinate, all at global scope/bus 0.
// The entire route array uses one property, for an atomic rewire across AU processes.
struct DisplayState {
    tri::Routing graph=tri::defaultRouting;
    tri::layout::State layout=tri::layout::defaults();
};
inline constexpr AudioUnitPropertyID display=0x54445350; // TDSP
inline constexpr AudioUnitPropertyID routes = 0x54524752; // TRGR
inline constexpr AudioUnitPropertyID layoutBase = 0x544c5900; // TLY + offset
inline constexpr AudioUnitPropertyID layoutProperty(int editorID) {
    return layoutBase + static_cast<UInt32>(editorID-tri::layout::first);
}
inline constexpr bool isLayoutProperty(AudioUnitPropertyID id) {
    return id>=layoutBase && id<layoutBase+tri::layout::count;
}
}
