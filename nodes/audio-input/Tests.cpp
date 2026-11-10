#include "Node.hpp"
#include <cassert>
#include <cstdio>
int main() {
    using namespace tri::nodes::audio_input;
    Kernel first,second;first.prepare(48000,8);second.prepare(48000,8);
    tri::KernelConfig configuration;configuration.parameters[InputGain]=.5f;first.configure(configuration);
    configuration.parameters[InputGain]=1;second.configure(configuration);
    float input[]={1,-1,.25f,.75f},right[]={-.5f,.5f,-.25f,.125f};
    float leftOut[]={9,9,9,9},rightOut[]={9,9,9,9};
    tri::KernelOutput output;output.id="audio";output.left=leftOut;output.right=rightOut;
    tri::KernelContext context;context.frames=3;context.outputs=&output;context.outputCount=1;
    context.hostAudioLeft=input;
    first.process(context);
    for(int i=0;i<3;++i)assert(leftOut[i]==input[i]*.5f&&rightOut[i]==leftOut[i]);
    assert(leftOut[3]==9&&rightOut[3]==9); // valid frame count is authoritative
    context.hostAudioRight=right;context.frames=1;
    second.process(context);assert(leftOut[0]==1&&rightOut[0]==-.5f);
    context.hostAudioLeft=input+3;context.hostAudioRight=right+3;
    first.process(context);assert(leftOut[0]==.375f&&rightOut[0]==.0625f); // next short block
    first.reset(tri::KernelResetReason::Stop);
    context.hostAudioLeft=context.hostAudioRight=nullptr;context.frames=4;
    first.process(context);for(int i=0;i<4;++i)assert(leftOut[i]==0&&rightOut[i]==0);
    context.outputCount=0;first.process(context);
    assert(input[0]==1&&right[3]==.125f);
    const auto spec=module().spec;
    assert(!spec.legacyAdapter&&spec.kernelFactory&&spec.type==tri::nodeTypeID(spec.key));
    puts("PASS: audio input mono/stereo, short blocks, independent gain and empty host input");
}
