#pragma once
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace tri {
// Decoded audio used by the sampler. Samples are interleaved float32 so the
// graph document can embed them without depending on a file path. Keeping the
// original sample rate lets the realtime voice resample with a playback step.
struct AudioSample {
    std::string name;
    double rate=0;
    int channels=0;
    std::vector<float> samples;

    size_t frames() const {
        return channels>0 ? samples.size()/size_t(channels) : 0;
    }
    double duration() const {
        return rate>0 ? double(frames())/rate : 0;
    }
    bool valid() const {
        return rate>=1000 && rate<=384000 && (channels==1 || channels==2) &&
            !samples.empty() && samples.size()%size_t(channels)==0;
    }
};
} // namespace tri
