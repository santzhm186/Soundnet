#include "Node.hpp"
#include <array>
#include <cassert>
#include <iostream>
using namespace tri;
using namespace tri::nodes::mixer;
int main(){
    Kernel kernel;kernel.prepare(48000,64);kernel.configure({});
    std::array<float,9> a{},b{},l{},r{};a.fill(.2f);b.fill(.3f);l.fill(42);r.fill(42);
    std::array<KernelInput,2> inputs{};inputs[0].id="in1";inputs[0].left=a.data();inputs[0].right=b.data();
    inputs[1].id="in4";inputs[1].left=b.data();inputs[1].right=a.data();
    KernelOutput output{};output.id="audio";output.left=l.data();output.right=r.data();
    KernelContext context{};context.frames=8;context.inputs=inputs.data();context.inputCount=2;context.outputs=&output;context.outputCount=1;
    kernel.process(context);for(int i=0;i<8;++i){assert(l[i]==.5f);assert(r[i]==.5f);}assert(l[8]==42);
    context.inputCount=0;kernel.process(context);for(int i=0;i<8;++i)assert(l[i]==0&&r[i]==0);
    kernel.reset(KernelResetReason::Stop);assert(module().spec.kernelFactory);
    std::cout<<"PASS: mixer sums declared ports and handles empty/short inputs\n";
}
