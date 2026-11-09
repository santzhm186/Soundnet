#include "Kernel.hpp"
#include <cassert>
#include <cstdio>
int main() {
    tri::nodes::timecode::Kernel kernel;kernel.prepare(96000,128);
    tri::KernelTimecode time;tri::KernelOutput output;output.id="time";output.timecode=&time;
    tri::KernelContext context;context.sampleRate=96000;context.framePosition=192000;
    context.timeGeneration=3;context.playing=true;context.frames=127;
    context.outputs=&output;context.outputCount=1;
    kernel.process(context);assert(time.framePosition==192000&&time.sampleRate==96000&&time.playing);
    context.framePosition=0;context.timeGeneration=4;context.playing=false;
    kernel.reset(tri::KernelResetReason::Seek);kernel.process(context);
    assert(time.framePosition==0&&time.generation==4&&!time.playing);
    puts("PASS: timecode standalone positions, generation and stopped state");
}
