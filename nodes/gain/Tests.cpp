#include "Kernel.hpp"
#include <cassert>
#include <cstdio>
int main() {
    tri::nodes::gain::Kernel kernel;kernel.prepare(48000,128);
    tri::KernelConfig config;config.parameters[tri::nodes::gain::amountID]=.5f;kernel.configure(config);
    float left[]={1,.5f,-.5f},right[]={-.25f,.75f,1},outL[3]{},outR[3]{};
    tri::KernelInput input;input.id="audio";input.left=left;input.right=right;
    tri::KernelOutput output;output.id="audio";output.left=outL;output.right=outR;
    tri::KernelContext context;context.frames=3;context.inputs=&input;context.inputCount=1;
    context.outputs=&output;context.outputCount=1;
    kernel.process(context);
    assert(outL[0]==.5f&&outR[0]==-.125f&&outL[2]==-.25f);
    context.bypassed=true;kernel.process(context);assert(outR[2]==1);
    context.inputs=nullptr;context.inputCount=0;kernel.process(context);assert(outL[0]==0&&outR[2]==0);
    puts("PASS: gain standalone stereo, tail block, bypass and empty input");
}
