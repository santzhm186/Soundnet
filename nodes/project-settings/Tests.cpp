#include "Node.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
int main() {
    using namespace tri::nodes::project_settings;
    const auto description=module();const auto& spec=description.spec;
    assert(spec.environmentRole==tri::EnvironmentRole::ProjectSettings&&spec.kernelFactory&&!spec.legacyAdapter);
    assert(spec.inputs.empty()&&spec.outputs.empty());
    tri::KernelConfig config;std::string why;
    assert(validate(config,why));
    for(float bits:{16.f,24.f,32.f}) {config.parameters[bitsID]=bits;assert(validate(config,why));}
    for(float bits:{0.f,20.f,24.5f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        config.parameters[bitsID]=bits;why.clear();assert(!validate(config,why)&&!why.empty());
    }
    assert(rateInfo.min==8000&&rateInfo.max==192000&&rateInfo.def==48000);
    assert(precisionInfo.min==32&&precisionInfo.max==32);
    bool folder=false,input=false,output=false;
    for(const auto& text:spec.texts) {
        if(std::string(text.id)=="project.folder")folder=std::string(text.defaultValue).empty()&&text.maxBytes>=4096;
        if(std::string(text.id)=="project.inputDevice")input=std::string(text.defaultValue)=="system";
        if(std::string(text.id)=="project.outputDevice")output=std::string(text.defaultValue)=="system";
    }
    assert(folder&&input&&output);
    auto kernel=spec.kernelFactory();kernel->prepare(48000,128);kernel->configure({});
    float untouched=123;tri::KernelOutput unrelated;unrelated.id="unrelated";unrelated.left=&untouched;
    tri::KernelContext context;context.frames=1;context.outputs=&unrelated;context.outputCount=1;
    kernel->process(context);kernel->reset(tri::KernelResetReason::Stop);assert(untouched==123);
    puts("PASS: project settings bit-depth validation, public defaults and inert environment processing");
}
