#pragma once
#include "NodeKernel.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>
namespace tri {
class AudioProcessor {
public:
    virtual ~AudioProcessor()=default;
    virtual void prepare(double sampleRate)=0;
    virtual void reset()=0;
    virtual void set(int param,float value)=0;
    virtual void render(const float* inL,const float* inR,float* outL,float* outR,int frames)=0;
    // Optional detector input for processors such as the compressor.
    virtual void setSidechain(const float*,const float*,int) {}
};

}
