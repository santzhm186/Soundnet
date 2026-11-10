#include "Node.hpp"
#include <cassert>
#include <cstdio>
int main() {
    using namespace tri::nodes::audio_output;
    Kernel kernel;kernel.prepare(48000,8);
    tri::KernelConfig config;config.parameters[Master]=.5f;config.parameters[OutputWidth]=0;kernel.configure(config);
    float left[]={1,-1,.25f},right[]={-.5f,.5f,.75f},outL[]={9,9,9},outR[]={9,9,9};
    tri::KernelInput input;input.id="audio";input.left=left;input.right=right;
    tri::KernelOutput output;output.id="audio";output.left=outL;output.right=outR;
    tri::KernelContext context;context.frames=2;context.inputs=&input;context.inputCount=1;
    context.outputs=&output;context.outputCount=1;
    kernel.process(context);assert(outL[0]==.125f&&outR[0]==.125f&&outL[1]==-.125f);
    assert(outL[2]==9&&outR[2]==9);
    config.parameters[OutputWidth]=2;kernel.configure(config);context.frames=3;
    kernel.process(context);assert(outL[0]==.875f&&outR[0]==-.625f);
    context.bypassed=true;kernel.process(context);
    for(int i=0;i<3;++i)assert(outL[i]==0&&outR[i]==0); // output sink bypass means mute
    context.bypassed=false;context.inputCount=0;kernel.process(context);
    for(int i=0;i<3;++i)assert(outL[i]==0&&outR[i]==0);
    kernel.reset(tri::KernelResetReason::Stop);context.outputCount=0;kernel.process(context);
    assert(left[0]==1&&right[2]==.75f);
    assert(module().spec.capabilities&tri::CapOutputSink);
    puts("PASS: audio output gain, width, short blocks, mute bypass and missing input");
}
