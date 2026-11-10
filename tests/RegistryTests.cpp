#include "NodeTestLookup.hpp"
// Every registered node must describe itself consistently. This catches drift
// between the catalog, parameter metadata and graph port validation.
#include "../src/GraphSpec.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
using namespace tri;

int main() {
    std::set<int> catalogSeen;
    for(int type:nodeCatalog()) assert(catalogSeen.insert(type).second);
    for(const auto& spec:nodeSpecs()) {
        if(!spec.instantiable)continue;
        const int type=spec.type;
        const NodeMeta* meta=nodeMeta(type);
        assert(meta && meta->title && *meta->title);
        assert(meta->category && *meta->category);
        GraphSpec graph;
        GraphNode node;
        node.id=1;node.type=type;node.nodeVersion=spec.nodeVersion;node.stateVersion=spec.stateVersion;
        graph.nodes.push_back(node);
        std::vector<size_t> order;
        assert(graphOrder(graph,order));
        for(int p:nodeParamList(type)) {
            assert(validGraphParam(type,p));
            const ParamInfo& info=graphParamInfo(p);
            assert(info.name && *info.name);
            assert(std::isfinite(info.min) && std::isfinite(info.max) && std::isfinite(info.def));
            assert(info.min<=info.def && info.def<=info.max);
            node.parameters[p]=info.def;
        }
        auto inputs=inputTypes(type);
        assert(inputs.size()<=4);
        for(auto signal:inputs)assert(signal!=Signal::None);
        for(int port=0;port<outputCount(type);++port)assert(outputType(type,port)!=Signal::None);
        if(meta->visibleInCatalog)assert(catalogSeen.count(type));
    }
    assert(nodeAcceptsParam(ntest::type("filter"),ntest::param("filter","filter.cutoff")));
    assert(!nodeAcceptsParam(ntest::type("noise"),ntest::param("filter","filter.cutoff")));
    assert(nodeAcceptsParam(ntest::type("audio-output"),ntest::param("audio-output","output.width")));
    assert(nodeAcceptsParam(ntest::type("midi-output"),ntest::param("midi-output","midiOut.channel")));
    puts("PASS: node registry metadata and ports are consistent");
}
