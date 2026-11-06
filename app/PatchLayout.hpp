#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace tri::layout {
// Editor callbacks address the nodes in tri::Node order, starting at MIDI (1).
// Canvas coordinates and visibility never enter the DSP or host automation list.
inline constexpr int nodeCount = 18;
inline constexpr int first = 1000;
constexpr int x(int node) { return (node<=10 ? first + (node - 1) * 2 : 1023 + (node-11)*2); }
constexpr int y(int node) { return x(node) + 1; }
inline constexpr int visibility = 1020;
inline constexpr int kDefaultVisible = 1023;
inline constexpr int count = y(nodeCount)-first+1;
inline constexpr int beginRouting = visibility + 1;
inline constexpr int commitRouting = visibility + 2;
constexpr bool contains(int id) { return (id>=first && id<=visibility) || (id>=1023 && id<=y(nodeCount)); }
using State = std::array<float,count>;
constexpr State defaults() {
    State state{};
    for(auto& value:state)value=-1;
    state[visibility-first]=kDefaultVisible;
    return state;
}
inline bool valid(int id,float value) {
    if ((id==beginRouting || id==commitRouting) && value==-1) return true;
    if (!contains(id) || !std::isfinite(value)) return false;
    if (id == visibility) return std::trunc(value)==value && value>=0 && value<=((1<<nodeCount)-1);
    return value==-1 || (value>=0 && value<=65535);
}
} // namespace tri::layout
