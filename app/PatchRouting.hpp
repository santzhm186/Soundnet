#pragma once
#include "../src/Engine.hpp"
#include "PatchLayout.hpp"
#include <optional>

namespace tri {
// One transaction per editor. A graph import or multi-cable edit either commits the
// final validated topology, or keeps the previous patch sounding.
class RouteEdit {
    std::optional<Routing> draft;
public:
    template <class ReadGraph,class WriteGraph>
    bool handle(int id, float value, ReadGraph read, WriteGraph write) {
        if (id==layout::beginRouting) {
            draft=read();
            return true;
        }
        if (id==layout::commitRouting) {
            if (draft && validRouting(*draft)) write(*draft);
            draft.reset();
            return true;
        }
        if (!routeParam(id)) return false;
        if (!std::isfinite(value) || value!=std::trunc(value) || value<0 || value>layout::nodeCount) return true;
        if (draft) {
            (*draft)[routeIndex(id)]=static_cast<int>(value);
        } else {
            auto next=read();
            next[routeIndex(id)]=static_cast<int>(value);
            if(validRouting(next)) write(next);
        }
        return true;
    }
};
} // namespace tri
