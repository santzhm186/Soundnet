#pragma once
// Public node execution contract. This header depends only on the C++ library.
// A kernel owns its state. The host prepares/configures it off the audio thread;
// process/reset run on the owning render thread and must not allocate or block.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include "resources/AudioSample.hpp"
#include "resources/MidiClip.hpp"
#include "resources/NoteModel.hpp"

namespace tri {
inline constexpr unsigned nodeSDKVersion=1;
constexpr uint32_t stableNodeHash(const char* text,uint32_t seed=2166136261u) {
    while(*text) {seed^=static_cast<unsigned char>(*text++);seed*=16777619u;}
    return seed;
}
constexpr int nodeTypeID(const char* nodeKey) {
    return int(0x40000000u|(stableNodeHash(nodeKey)&0x3fffffffu));
}
constexpr int parameterID(const char* nodeKey,const char* localKey) {
    return int(0x40000000u|(stableNodeHash(localKey,stableNodeHash("/",stableNodeHash(nodeKey)))&0x3fffffffu));
}
struct KernelConfig {
    std::map<int,float> parameters;
    std::map<std::string,std::string> texts;
    // Immutable resources are prepared before publication and retained for the
    // kernel lifetime. No decoder or resource ownership change occurs in process.
    std::shared_ptr<const AudioSample> audio;
    std::shared_ptr<const MidiClip> clip;
    std::shared_ptr<const NoteModel> model;
    float value(int id,float fallback=0) const {
        auto i=parameters.find(id);return i==parameters.end()?fallback:i->second;
    }
    const std::string& text(const std::string& key) const {
        static const std::string empty;
        auto i=texts.find(key);return i==texts.end()?empty:i->second;
    }
};
enum class KernelResetReason { Stop, Panic, Seek, Reconfigure, Removed };
enum class EnvironmentRole { None, Timecode, ProjectSettings };
struct KernelMidiEvent { int frame=0; uint8_t status=0,a=0,b=0; };
struct KernelTriggerEvent { int frame=0; bool start=false; };
struct KernelTimecode {
    uint64_t framePosition=0,generation=0;
    double sampleRate=48000;
    bool playing=false;
};
struct KernelInput {
    const char* id=nullptr;
    const float* left=nullptr;
    const float* right=nullptr;
    const KernelMidiEvent* midi=nullptr;
    size_t midiCount=0;
    const KernelTriggerEvent* triggers=nullptr;
    size_t triggerCount=0;
    const KernelTimecode* timecode=nullptr;
};
struct KernelOutput {
    const char* id=nullptr;
    float* left=nullptr;
    float* right=nullptr;
    KernelMidiEvent* midi=nullptr;
    size_t midiCount=0,midiCapacity=0;
    KernelTriggerEvent* triggers=nullptr;
    size_t triggerCount=0,triggerCapacity=0;
    KernelTimecode* timecode=nullptr;
    bool overflow=false;
    bool emitMidi(KernelMidiEvent event) noexcept {
        if(!midi||midiCount>=midiCapacity) {overflow=true;return false;}
        midi[midiCount++]=event;return true;
    }
    bool emitTrigger(KernelTriggerEvent event) noexcept {
        if(!triggers||triggerCount>=triggerCapacity) {overflow=true;return false;}
        triggers[triggerCount++]=event;return true;
    }
};
struct KernelContext {
    double sampleRate=48000;
    int frames=0;
    uint64_t framePosition=0,timeGeneration=0;
    bool playing=false;
    bool bypassed=false;
    const KernelInput* inputs=nullptr;
    size_t inputCount=0;
    KernelOutput* outputs=nullptr;
    size_t outputCount=0;
    const KernelInput* input(const char* id) const noexcept {
        for(size_t i=0;i<inputCount;++i)if(inputs[i].id&&std::strcmp(inputs[i].id,id)==0)return inputs+i;
        return nullptr;
    }
    KernelOutput* output(const char* id) const noexcept {
        for(size_t i=0;i<outputCount;++i)if(outputs[i].id&&std::strcmp(outputs[i].id,id)==0)return outputs+i;
        return nullptr;
    }
};
class NodeKernel {
public:
    virtual ~NodeKernel()=default;
    virtual void prepare(double sampleRate,int maxFrames)=0;
    virtual void configure(const KernelConfig& config)=0;
    virtual void process(KernelContext& context) noexcept=0;
    virtual void reset(KernelResetReason reason) noexcept=0;
};
using KernelFactory=std::unique_ptr<NodeKernel>(*)();
using ConfigValidator=bool(*)(const KernelConfig&,std::string&);
} // namespace tri
