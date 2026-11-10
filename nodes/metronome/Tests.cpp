#include "Node.hpp"
#include <cassert>
#include <cstdio>
#include <array>
using namespace tri;
#include <cmath>
using namespace tri::nodes::metronome;
int main(){Kernel k;k.prepare(48000,128);k.configure({});KernelTriggerEvent events[]={{7,true},{30,false}};KernelInput in;in.id="trigger";in.triggers=events;in.triggerCount=2;float left[64]{},right[64]{};KernelOutput out;out.id="audio";out.left=left;out.right=right;KernelContext c;c.frames=64;c.inputs=&in;c.inputCount=1;c.outputs=&out;c.outputCount=1;k.process(c);for(int i=0;i<8;++i)assert(left[i]==0);assert(std::fabs(left[9])>0);for(int i=30;i<64;++i)assert(left[i]==0&&right[i]==0);k.reset(KernelResetReason::Stop);in.triggerCount=0;k.process(c);for(float v:left)assert(v==0);puts("PASS metronome: frame-accurate start/stop and reset silence");}
