#include "../platform/ProjectEnvironment.hpp"
#include <cassert>
#include <cstdio>

int main() {
    tri::GraphSpec graph;
    tri::ProjectEnvironment env;std::string why;
    assert(tri::projectEnvironment(graph,env,&why)&&!env.present);
    tri::GraphNode settings;
    settings.id=1;settings.key="soundnet.project-settings";
    settings.type=tri::nodeTypeForKey(settings.key.c_str());
    assert(settings.type);
    graph.nodes.push_back(settings);
    assert(tri::projectEnvironment(graph,env,&why)&&env.present);
    assert(env.sampleRate==48000&&env.exportBits==24&&env.processingPrecision==32);
    assert(env.inputDevice=="system"&&env.outputDevice=="system");
    const int rate=tri::nodeParamIdForKey(settings.type,"project.sampleRate");
    const int bits=tri::nodeParamIdForKey(settings.type,"project.exportBits");
    assert(rate>=0&&bits>=0);
    graph.nodes[0].parameters[rate]=44100;
    graph.nodes[0].parameters[bits]=32;
    graph.nodes[0].texts["project.outputDevice"]="ExampleDeviceUID";
    assert(tri::projectEnvironment(graph,env,&why));
    assert(env.sampleRate==44100&&env.exportBits==32&&env.outputDevice=="ExampleDeviceUID");
    auto previous=env;
    graph.nodes[0].parameters[bits]=20;
    assert(!tri::projectEnvironment(graph,env,&why));
    assert(env.sampleRate==previous.sampleRate&&env.exportBits==previous.exportBits);
    graph.nodes[0].parameters[bits]=24;
    graph.nodes.push_back(settings);graph.nodes.back().id=2;
    assert(!tri::projectEnvironment(graph,env,&why));
    graph.nodes.pop_back();graph.nodes[0].unavailable=true;
    assert(!tri::projectEnvironment(graph,env,&why));
    puts("PASS: project environment defaults, format validation and singleton settings");
}
