#include "NodeTestLookup.hpp"
#include "../src/GraphSpec.hpp"
#include <cassert>
#include <cstdio>
using namespace tri;

// This single-threaded fixture temporarily replaces a vector-owned descriptor,
// restoring it before exit. Production code never mutates the frozen registry.
struct DescriptorFixture {
    NodeSpec& spec;
    NodeSpec saved;
    DescriptorFixture():spec(*const_cast<NodeSpec*>(nodeSpecForKey("soundnet.gain"))),saved(spec) {}
    ~DescriptorFixture() {spec=std::move(saved);}
};
static GraphNode node(InstanceID id,const char* key) {
    GraphNode value;value.id=id;value.key=key;return value;
}
static GraphEdge edge(InstanceID source,const char* out,InstanceID target,const char* in) {
    GraphEdge value;value.source=source;value.target=target;value.sourcePortId=out;value.portId=in;return value;
}
static bool validated=false;
static bool resolvedValidator(const KernelConfig& config,std::string&) {
    assert(config.parameters.at(nodes::gain::amountID)==1);
    assert(config.texts.at("greeting")=="hello");
    assert(config.audio && config.audio->samples.at(0)==.25f);
    assert(config.clip && config.clip->events.at(0).data1==60);
    assert(config.model && config.model->pitches.at(60)==61);
    validated=true;return true;
}
int main() {
    assert(registryProblems().empty());
    CompiledGraph plan;
    {
        DescriptorFixture fixture;
        fixture.spec.texts={{"greeting","Greeting","hello"}};
        fixture.spec.capabilities|=CapAssetAudio|CapAssetClip|CapAssetModel;
        fixture.spec.validateConfig=resolvedValidator;
        GraphSpec graph;graph.nodes={node(1,"soundnet.gain")};
        auto audio=std::make_shared<AudioSample>();audio->samples={.25f};
        graph.nodes[0].sample=audio;
        graph.nodes[0].clip.events={{0,0x90,60,100}};
        graph.nodes[0].model.pitches[60]=61;
        assert(compileGraph(graph,plan)&&validated);
        const auto config=resolvedKernelConfig(graph.nodes[0]);
        std::string why;assert(resolvedValidator(config,why));
        graph.nodes[0].parameters[nodes::gain::amountID]=2;
        graph.nodes[0].texts["greeting"]="";
        const auto explicitConfig=resolvedKernelConfig(graph.nodes[0]);
        assert(explicitConfig.value(nodes::gain::amountID)==2&&explicitConfig.text("greeting").empty());
        fixture.spec.capabilities&=~(CapAssetAudio|CapAssetClip|CapAssetModel);
        const auto unclaimed=resolvedKernelConfig(graph.nodes[0]);
        assert(!unclaimed.audio&&!unclaimed.clip&&!unclaimed.model);
    }
    {
        DescriptorFixture fixture;
        GraphSpec graph;graph.nodes={node(1,"soundnet.gain")};
        fixture.spec.inputs[0].flags=PortOptional|PortFeedback;
        assert(!compileGraph(graph,plan));
        fixture.spec.inputs[0].flags=PortOptional|PortMultiSource;
        assert(!compileGraph(graph,plan));
        fixture.spec.inputs[0].flags=PortOptional;
        fixture.spec.inputs[0].role=RoleTrigger;
        assert(!compileGraph(graph,plan));
        fixture.spec.inputs[0].role=RoleMain;
        fixture.spec.inputs[0].flags=0;
        assert(compileGraph(graph,plan)&&!plan.fatal()&&!plan.diagnostics.empty());
        fixture.spec.inputs[0].flags=PortOptional;
        assert(compileGraph(graph,plan)&&plan.diagnostics.empty());
    }
    {
        DescriptorFixture fixture;
        // Trigger routing uses the signal, independently of the display role.
        fixture.spec.inputs={{"request","Request",Signal::Trigger,RoleData,PortOptional}};
        GraphSpec graph;graph.nodes={node(1,"soundnet.start"),node(2,"soundnet.start"),node(3,"soundnet.gain")};
        graph.edges={edge(1,"trigger",3,"request"),edge(1,"trigger",3,"request")};
        assert(compileGraph(graph,plan)&&plan.nodes[2].triggerInputs[0].size()==1);
        graph.edges.push_back(edge(2,"trigger",3,"request"));
        assert(!compileGraph(graph,plan));
        fixture.spec.inputs[0].flags|=PortMultiSource;
        assert(compileGraph(graph,plan)&&plan.nodes[2].triggerInputs[0].size()==2);
        assert(registryProblems().empty());
    }
    {
        DescriptorFixture fixture;
        fixture.spec.inputs={{"time","Time",Signal::Clock,RoleClock,PortOptional}};
        GraphSpec graph;graph.nodes={node(1,"soundnet.timecode"),node(2,"soundnet.gain")};
        graph.edges={edge(1,"time",2,"time")};assert(compileGraph(graph,plan));
        graph.nodes[0]=node(1,"soundnet.clock");assert(!compileGraph(graph,plan));
        graph.nodes[0]=node(1,"soundnet.daw-clock");assert(!compileGraph(graph,plan));
    }
    assert(registryProblems().empty());
    puts("PASS: resolved configuration, declared resources, port flags and clock compatibility");
}
