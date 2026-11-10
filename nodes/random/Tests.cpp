#include "Node.hpp"
#include <cassert>
#include <cstdio>
#include <array>
using namespace tri;
using namespace tri::nodes::random;
int main(){Kernel k;k.prepare(48000,128);KernelConfig config;config.parameters[RandomChance]=0;k.configure(config);KernelTriggerEvent events[]={{3,true},{7,false}},result[4]{};KernelInput in;in.id="trigger";in.triggers=events;in.triggerCount=2;KernelOutput out;out.id="trigger";out.triggers=result;out.triggerCapacity=4;KernelContext c;c.instanceID=42;c.frames=8;c.inputs=&in;c.inputCount=1;c.outputs=&out;c.outputCount=1;k.process(c);assert(out.triggerCount==0);config.parameters[RandomChance]=1;k.configure(config);k.process(c);assert(out.triggerCount==2&&result[0].frame==3&&result[1].frame==7&&!result[1].start);puts("PASS random: deterministic probability endpoints and event preservation");}
