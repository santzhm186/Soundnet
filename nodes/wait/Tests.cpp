#include "Node.hpp"
#include <cassert>
#include <cstdio>
#include <array>
using namespace tri;
using namespace tri::nodes::wait;
int main(){Kernel k;k.prepare(1000,128);KernelConfig config;config.parameters[WaitSeconds]=.01f;k.configure(config);KernelTriggerEvent events[]={{3,true},{4,false}},result[4]{};KernelInput in;in.id="trigger";in.triggers=events;in.triggerCount=2;KernelOutput out;out.id="trigger";out.triggers=result;out.triggerCapacity=4;KernelContext c;c.frames=8;c.inputs=&in;c.inputCount=1;c.outputs=&out;c.outputCount=1;k.process(c);assert(out.triggerCount==0);in.triggerCount=0;k.process(c);assert(out.triggerCount==2&&result[0].frame==5&&result[1].frame==6&&!result[1].start);in.triggerCount=1;k.process(c);k.reset(KernelResetReason::Stop);in.triggerCount=0;out.triggerCount=0;k.process(c);assert(out.triggerCount==0);puts("PASS wait: delay across short blocks, event order and cancellation");}
