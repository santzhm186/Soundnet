#include "Node.hpp"
#include <cassert>
#include <cstdio>
#include <array>
using namespace tri;
using namespace tri::nodes::count;
int main(){Kernel k;k.prepare(48000,128);KernelConfig config;config.parameters[TriggerCount]=3;k.configure(config);KernelTriggerEvent events[]={{1,true},{2,false},{7,true}},result[4]{};KernelInput in;in.id="trigger";in.triggers=events;in.triggerCount=2;KernelOutput out;out.id="trigger";out.triggers=result;out.triggerCapacity=4;KernelContext c;c.frames=8;c.inputs=&in;c.inputCount=1;c.outputs=&out;c.outputCount=1;k.process(c);assert(out.triggerCount==0);in.triggers=events+2;in.triggerCount=1;k.process(c);assert(out.triggerCount==1&&result[0].frame==7);k.reset(KernelResetReason::Stop);out.triggerCount=0;k.process(c);assert(out.triggerCount==0);puts("PASS count: cross-block state, polarity preservation and reset");}
