#include "Node.hpp"
#include <cassert>
#include <cstdio>
#include <array>
using namespace tri;
using namespace tri::nodes::stop;
int main(){Kernel k;k.prepare(48000,128);KernelConfig config;config.parameters[TriggerPulse]=9;k.configure(config);
KernelTriggerEvent events[]={5,false},results[4]{};KernelInput in;in.id="trigger";in.triggers=events;in.triggerCount=1;KernelOutput out;out.id="trigger";out.triggers=results;out.triggerCapacity=4;KernelContext c;c.frames=7;c.inputs=&in;c.inputCount=1;c.outputs=&out;c.outputCount=1;k.process(c);assert(out.triggerCount==1&&results[0].frame==5&&results[0].start==false);
in.triggerCount=0;out.triggerCount=0;config.hasPreviousConfiguration=true;config.previousParameters[TriggerPulse]=9;config.parameters[TriggerPulse]=10;k.configure(config);k.process(c);assert(out.triggerCount==1&&results[0].frame==0&&results[0].start==false);k.reset(KernelResetReason::Stop);out.triggerCount=0;k.process(c);assert(out.triggerCount==0);puts("PASS stop: polarity, exact frame, pulse change and reset");}
