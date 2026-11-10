#include <AudioUnitSDK/MusicDeviceBase.h>
#include "WorkflowProperties.hpp"
#import "../platform/NodePackageLoader.h"
#include "../src/MidiCaptureAccess.hpp"
#include "../app/WorkflowAssetStore.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <bitset>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <dlfcn.h>

namespace {
constexpr UInt32 global = kAudioUnitScope_Global;

struct Event {
    UInt32 frame;
    uint64_t order;
    uint8_t status, a, b;
    bool local = false;
    uint64_t generation = 0;
};

// Host and onscreen keyboard may enqueue from separate threads.
class MidiQueue {
    static constexpr size_t capacity = 2048;
    struct Slot { std::atomic<uint64_t> sequence{0}; Event event{}; };
    std::array<Slot,capacity> slots{};
    std::atomic<uint64_t> write{0};
    uint64_t read = 0;
public:
    MidiQueue() {
        for (size_t i = 0; i < capacity; ++i) slots[i].sequence.store(i);
    }
    bool push(Event input) noexcept {
        uint64_t pos = write.load(std::memory_order_relaxed);
        for (unsigned attempt=0; attempt<32; ++attempt) {
            Slot& slot = slots[pos % capacity];
            const auto diff = static_cast<int64_t>(slot.sequence.load(std::memory_order_acquire) - pos);
            if (diff < 0) return false;
            if (diff > 0) {
                pos = write.load(std::memory_order_relaxed);
            } else if (write.compare_exchange_weak(pos,pos+1,std::memory_order_relaxed)) {
                input.order = pos;
                slot.event = input;
                slot.sequence.store(pos+1,std::memory_order_release);
                return true;
            }
        }
        return false;
    }
    bool pop(Event& event) noexcept {
        Slot& slot = slots[read % capacity];
        if (slot.sequence.load(std::memory_order_acquire) != read+1) return false;
        event = slot.event;
        slot.sequence.store(read+capacity,std::memory_order_release);
        ++read;
        return true;
    }
};

AudioUnitParameterUnit unitForID(int id) {
    using namespace tri;
    switch(id) {
        case Wave1: case Wave2: case Wave3: case Pattern: case ArpRepeat: return kAudioUnitParameterUnit_Indexed;
        case ArpOn: return kAudioUnitParameterUnit_Boolean;
        case Tune1: case Tune2: case Tune3: return kAudioUnitParameterUnit_RelativeSemiTones;
        case Attack: case Decay: case Release:
        case Osc1Attack: case Osc2Attack: case Osc3Attack:
        case Osc1Release: case Osc2Release: case Osc3Release: return kAudioUnitParameterUnit_Seconds;
        case Osc1Sustain: case Osc2Sustain: case Osc3Sustain: return kAudioUnitParameterUnit_LinearGain;
        case EQLow: case EQMid: case EQHigh: return kAudioUnitParameterUnit_Decibels;
        case DelayTime: return kAudioUnitParameterUnit_Milliseconds;
        case Tempo: return kAudioUnitParameterUnit_BPM;
        default: return kAudioUnitParameterUnit_Generic;
    }
}

bool isLocalEnvelope(UInt32 id) {
    return id >= tri::Osc1Attack && id <= tri::Osc3Release;
}

// The SDK serializes one record per populated scope/bus, then (ID, float) pairs.
// Identify omitted parameters before passing bounded, validated data to its reader.
bool savedParameters(CFPropertyListRef state, std::bitset<tri::ParamCount>& present) {
    if (!state || CFGetTypeID(state) != CFDictionaryGetTypeID()) return false;
    auto dict = static_cast<CFDictionaryRef>(state);
    auto data = static_cast<CFDataRef>(CFDictionaryGetValue(dict,CFSTR(kAUPresetDataKey)));
    if (!data || CFGetTypeID(data) != CFDataGetTypeID()) return false;
    const auto* bytes = CFDataGetBytePtr(data);
    const size_t length = static_cast<size_t>(CFDataGetLength(data));
    size_t i = 0;
    auto nextWord = [&] {
        uint32_t be = 0;
        std::memcpy(&be,bytes+i,sizeof(be));
        i += sizeof(be);
        return CFSwapInt32BigToHost(be);
    };
    while (i < length) {
        if (length-i < 12) return false;
        const auto scope = nextWord(), element = nextWord(), count = nextWord();
        if (scope > kAudioUnitScope_Output || count > (length-i)/8) return false;
        for (UInt32 j = 0; j < count; ++j) {
            const auto id = nextWord();
            const auto value = std::bit_cast<float>(nextWord());
            if (!std::isfinite(value)) return false;
            if (scope == global && element == 0 && id < tri::ParamCount) present.set(id);
        }
    }
    return true;
}

template<class T, size_t N>
void saveArray(CFMutableDictionaryRef state,CFStringRef key,const std::array<T,N>& values) {
    auto array=CFArrayCreateMutable(nullptr,N,&kCFTypeArrayCallBacks);
    for(T value:values) {
        const double number=value;
        auto boxed=CFNumberCreate(nullptr,kCFNumberDoubleType,&number);
        CFArrayAppendValue(array,boxed);
        CFRelease(boxed);
    }
    CFDictionarySetValue(state,key,array);
    CFRelease(array);
}

// Missing arrays indicate a 1.0/1.1 session. Keep the caller's default graph and layout.
template<class T, size_t N,class Test>
bool loadArray(CFDictionaryRef state,CFStringRef key,std::array<T,N>& values,Test valid) {
    auto value=static_cast<CFArrayRef>(CFDictionaryGetValue(state,key));
    if (!value) return true;
    if (CFGetTypeID(value)!=CFArrayGetTypeID() || CFArrayGetCount(value)>N) return false;
    for(size_t i=0;i<(size_t)CFArrayGetCount(value);++i) {
        auto boxed=static_cast<CFNumberRef>(CFArrayGetValueAtIndex(value,i));
        double number=0;
        if (!boxed || CFGetTypeID(boxed)!=CFNumberGetTypeID() ||
            !CFNumberGetValue(boxed,kCFNumberDoubleType,&number) || !std::isfinite(number) ||
            !valid(i,number)) return false;
        values[i]=static_cast<T>(number);
    }
    return true;
}

// Derive the UI bundle path from this binary so separate copies work correctly.
CFURLRef copyEditorURL() {
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(&copyEditorURL), &info) || !info.dli_fname) return nullptr;
    auto url = CFURLCreateFromFileSystemRepresentation(nullptr,
        reinterpret_cast<const UInt8*>(info.dli_fname), strlen(info.dli_fname), false);
    if (!url) return nullptr;
    auto macOS = CFURLCreateCopyDeletingLastPathComponent(nullptr, url);
    CFRelease(url);
    auto contents = CFURLCreateCopyDeletingLastPathComponent(nullptr, macOS);
    CFRelease(macOS);
    auto resources = CFURLCreateCopyAppendingPathComponent(nullptr,contents,CFSTR("Resources"),true);
    CFRelease(contents);
    auto result = CFURLCreateCopyAppendingPathComponent(nullptr,resources,CFSTR("TernaryView.bundle"),true);
    CFRelease(resources);
    return result;
}
}

class TernaryUnit final : public ausdk::MusicDeviceBase {
    using Base = ausdk::MusicDeviceBase;
    tri::Engine engine;
    tri::WorkflowAssetStore assetStore;
    std::mutex captureReplyLock;
    NSDictionary *captureReply=@{};
    // The property thread publishes a matching function/context pair for the next
    // render. Packet timestamps are sample offsets from the original host buffer.
    tri::RealtimeAsset<AUMIDIOutputCallbackStruct> midiDelivery;
    const AudioTimeStamp *renderStamp=nullptr;
    UInt32 midiOffset=0;
    UInt16 resetChannels=0;
    static void emitEvent(void *context,int frame,UInt8 status,UInt8 a,UInt8 b) {
        auto &unit=*static_cast<TernaryUnit *>(context);
        if (!unit.renderStamp) {
            // A host may reset the unit between render calls. MIDI outputs may
            // only call back during rendering, so flush affected channels then.
            unit.resetChannels |= UInt16(1u<<(status&15));
            return;
        }
        const auto &delivery=unit.midiDelivery.current();
        if (!delivery.midiOutputCallback) return;
        MIDIPacketList list{};
        list.numPackets=1;
        auto &packet=list.packet[0];
        packet.timeStamp=unit.midiOffset+frame;
        const int type=status&0xf0;
        packet.length=(type==0xc0 || type==0xd0)?2:3;
        packet.data[0]=status; packet.data[1]=a; packet.data[2]=b;
        delivery.midiOutputCallback(delivery.userData,unit.renderStamp,0,&list);
    }
    OSStatus localEvent(ternaryAU::LocalMIDI event) {
        const uint64_t generation=engine.processingGeneration();
        if(!engine.processingEnabled())return noErr;
        if (event.status<0x80 || event.status>0xef || event.a>127 || event.b>127)
            return kAudioUnitErr_InvalidPropertyValue;
        if (!queue.push({0,0,event.status,event.a,event.b,true,generation}))
            overflow.store(true,std::memory_order_release);
        return noErr;
    }
    std::array<std::atomic<float>,tri::layout::count> layout{};
    void replaceLayout(const tri::layout::State& state) {
        for (size_t i=0;i<state.size();++i) layout[i].store(state[i],std::memory_order_relaxed);
    }
    MidiQueue queue;
    std::atomic<bool> overflow{false},discardMidiRequested{false};
    std::array<Event,2048> pending{};
    size_t count = 0;

    void discardEvents() {
        Event event{};
        for (size_t i=0; i<pending.size() && queue.pop(event); ++i) {}
        count = 0;
        overflow.store(false);
    }
public:
    explicit TernaryUnit(AudioComponentInstance instance) : Base(instance,1,1) {
        replaceLayout(tri::layout::defaults());
        engine.setMidiOutput(this,emitEvent);
        CreateElements();
        Globals()->UseIndexedParameters(tri::ParamCount);
        for (int i=0; i<tri::ParamCount; ++i) SetParameterRT(i, tri::params[i].def);
        Input(0).SetName(CFSTR("Side Chain"));
        Output(0).SetName(CFSTR("Stereo Out"));
    }

    bool CanScheduleParameters() const noexcept override { return false; }
    bool StreamFormatWritable(AudioUnitScope, AudioUnitElement) override { return !IsInitialized(); }
    bool ValidFormat(AudioUnitScope scope, AudioUnitElement bus, const AudioStreamBasicDescription& f) override {
        const bool channels = f.mChannelsPerFrame == 2 || (scope == kAudioUnitScope_Input && f.mChannelsPerFrame == 1);
        return channels && f.mSampleRate >= 8000 && f.mSampleRate <= 384000 && Base::ValidFormat(scope,bus,f);
    }
    UInt32 SupportedNumChannels(const AUChannelInfo** info) override {
        static const AUChannelInfo channels[] = {{1,2},{2,2}};
        if (info) *info = channels;
        return 2;
    }
    OSStatus Initialize() override {
        const auto loaded=tri::refreshNodePackages(tri::defaultNodePackageRoots(tri::nodePlatformBundle()),true);
        if(!loaded.ok)return kAudioUnitErr_FailedInitialization;
        const auto rate = Output(0).GetStreamFormat().mSampleRate;
        if (Input(0).IsActive() && Input(0).GetStreamFormat().mSampleRate != rate)
            return kAudioUnitErr_FormatNotSupported;
        engine.prepare(rate);
        discardEvents();
        return Base::Initialize();
    }
    OSStatus Reset(AudioUnitScope scope, AudioUnitElement element) override {
        engine.reset();
        discardEvents();
        return Base::Reset(scope,element);
    }
    bool SupportsTail() override { return true; }
    Float64 GetTailTime() override {
        const double delay = GetParameterRT(tri::DelayTime)*0.001;
        const double feedback = std::clamp<double>(GetParameterRT(tri::DelayFeedback),0.0001,0.995);
        const double repeats = std::ceil(std::log(0.0001)/std::log(feedback));
        double release = 0;
        for (int id : {tri::Osc1Release,tri::Osc2Release,tri::Osc3Release}) {
            const float local = GetParameterRT(id);
            release = std::max(release,static_cast<double>(local < 0 ? GetParameterRT(tri::Release) : local));
        }
        return release + (GetParameterRT(tri::DelayMix)>0 ? delay*(1+repeats) : 0);
    }
    OSStatus SaveState(CFPropertyListRef* out) override {
        CFPropertyListRef base=nullptr;
        const OSStatus result=Base::SaveState(&base);
        if (result) return result;
        auto state=CFDictionaryCreateMutableCopy(nullptr,0,static_cast<CFDictionaryRef>(base));
        CFRelease(base);
        CFDictionarySetValue(state,CFSTR("Soundnet.Assets.1"),(__bridge CFDictionaryRef)assetStore.current);
        saveArray(state,CFSTR("Ternary.Routes.1"),engine.routing());
        tri::layout::State positions{};
        for(size_t i=0;i<positions.size();++i) positions[i]=layout[i].load(std::memory_order_relaxed);
        saveArray(state,CFSTR("Ternary.Layout.1"),positions);
        *out=state;
        return noErr;
    }
    OSStatus RestoreState(CFPropertyListRef state) override {
        // Apple's remote AUv2 proxy transports ClassInfo, but filters vendor properties.
        // A targeted editor command changes only wiring or one coordinate. It cannot
        // replay stale parameters over Logic automation.
        if (state && CFGetTypeID(state)==CFDictionaryGetTypeID()) {
            auto packet=static_cast<CFDictionaryRef>(state);
            auto command=static_cast<CFNumberRef>(CFDictionaryGetValue(packet,CFSTR("Ternary.EditorCommand")));
            if (command) {
                int operation=-1;
                if(CFGetTypeID(command)!=CFNumberGetTypeID() || !CFNumberGetValue(command,kCFNumberIntType,&operation))
                    return kAudioUnitErr_InvalidPropertyValue;
                if(operation==ternaryAU::refreshNodeLibrary) {
                    const auto refreshed=tri::refreshNodePackages(tri::defaultNodePackageRoots(tri::nodePlatformBundle()),true);
                    if(!refreshed.ok)return kAudioUnitErr_InvalidPropertyValue;
                    engine.setProcessingEnabled(false);
                    if(!assetStore.replace(assetStore.current,engine))return kAudioUnitErr_InvalidPropertyValue;
                    PropertyChanged(kAudioUnitProperty_ClassInfo,global,0);
                    PropertyChanged(ternaryAU::processing,global,0);
                    return noErr;
                }
                if (operation==ternaryAU::playLocalMIDI) {
                    ternaryAU::LocalMIDI event{};
                    auto bytes=static_cast<CFDataRef>(CFDictionaryGetValue(packet,CFSTR("Soundnet.LocalMIDI")));
                    if (!bytes || CFGetTypeID(bytes)!=CFDataGetTypeID() || CFDataGetLength(bytes)!=sizeof(event))
                        return kAudioUnitErr_InvalidPropertyValue;
                    std::memcpy(&event,CFDataGetBytePtr(bytes),sizeof(event));
                    return localEvent(event);
                }
                if (operation==ternaryAU::editAssets) {
                    NSDictionary *assets=(__bridge NSDictionary *)CFDictionaryGetValue(packet,CFSTR("Soundnet.Assets.1"));
                    if (!assetStore.replace(assets,engine)) return kAudioUnitErr_InvalidPropertyValue;
                } else if(operation==tri::layout::commitRouting) {
                    tri::Routing graph{};
                    if(!CFDictionaryContainsKey(packet,CFSTR("Ternary.Routes.1")) ||
                       !loadArray(packet,CFSTR("Ternary.Routes.1"),graph,[](size_t,double n) {
                           return n>=0 && n<=tri::layout::nodeCount && n==std::trunc(n);
                       }) || !engine.setRouting(graph)) return kAudioUnitErr_InvalidPropertyValue;
                } else if (tri::layout::contains(operation)) {
                    auto pos=static_cast<CFNumberRef>(CFDictionaryGetValue(packet,CFSTR("Ternary.Coordinate")));
                    float value=0;
                    if (!pos || CFGetTypeID(pos)!=CFNumberGetTypeID() ||
                        !CFNumberGetValue(pos,kCFNumberFloatType,&value) || !tri::layout::valid(operation,value))
                        return kAudioUnitErr_InvalidPropertyValue;
                    layout[operation-tri::layout::first].store(value,std::memory_order_relaxed);
                } else return kAudioUnitErr_InvalidPropertyValue;
                PropertyChanged(kAudioUnitProperty_ClassInfo,global,0);
                return noErr;
            }
        }
        std::bitset<tri::ParamCount> present;
        if (!savedParameters(state,present)) return kAudioUnitErr_InvalidPropertyValue;
        auto dict=static_cast<CFDictionaryRef>(state);
        auto graph=tri::defaultRouting;
        auto positions=tri::layout::defaults();
        if (!loadArray(dict,CFSTR("Ternary.Routes.1"),graph,[](size_t,double n) {
                return n>=0 && n<=tri::layout::nodeCount && std::trunc(n)==n;
            }) || !tri::validRouting(graph)) return kAudioUnitErr_InvalidPropertyValue;
        if (!loadArray(dict,CFSTR("Ternary.Layout.1"),positions,[](size_t i,double n) {
                return tri::layout::valid(tri::layout::first+static_cast<int>(i),static_cast<float>(n));
            })) return kAudioUnitErr_InvalidPropertyValue;
        tri::PreparedAssets assets;
        NSDictionary *savedAssets=(__bridge NSDictionary *)CFDictionaryGetValue(dict,CFSTR("Soundnet.Assets.1"));
        if (!assets.load(savedAssets ?: @{})) return kAudioUnitErr_InvalidPropertyValue;
        const OSStatus result = Base::RestoreState(state);
        if (result) return result;
        if(!assetStore.apply(assets,engine))return kAudioUnitErr_InvalidPropertyValue;
        engine.setRouting(graph);
        replaceLayout(positions);
        // Older projects omit the oscillator envelopes and repeat count. Reset these
        // even when importing an old preset into an instance that was already edited.
        for (int id=tri::Osc1Attack; id<tri::ParamCount; ++id)
            if (!tri::routeParam(id) && !present[id]) SetParameterRT(id,tri::params[id].def);
        return noErr;
    }
    OSStatus GetParameterList(AudioUnitScope scope, AudioUnitParameterID* ids, UInt32& count) override {
        if(scope!=global)return Base::GetParameterList(scope,ids,count);
        count=0;
        for(int id=0;id<tri::ParamCount;++id)if(!tri::routeParam(id)) {
            if(ids)ids[count]=id;
            ++count;
        }
        return noErr;
    }
    OSStatus GetParameterInfo(AudioUnitScope scope,AudioUnitParameterID id, AudioUnitParameterInfo& info) override {
        if (scope != global || id >= tri::ParamCount) return kAudioUnitErr_InvalidParameter;
        const auto& spec = tri::params[id];
        info = {};
        info.flags = kAudioUnitParameterFlag_IsReadable | kAudioUnitParameterFlag_IsWritable;
        if (isLocalEnvelope(id)) info.flags |= kAudioUnitParameterFlag_ValuesHaveStrings;
        info.minValue=spec.min; info.maxValue=spec.max; info.defaultValue=spec.def;
        info.unit = unitForID(id);
        auto name = CFStringCreateWithCString(nullptr,spec.name,kCFStringEncodingUTF8);
        FillInParameterName(info,name,true);
        return noErr;
    }
    OSStatus GetParameterValueStrings(AudioUnitScope scope, AudioUnitParameterID id, CFArrayRef* strings) override {
        if (scope != global) return kAudioUnitErr_InvalidScope;
        bool wave = id == tri::Wave1 || id == tri::Wave2 || id == tri::Wave3;
        if (!wave && id != tri::Pattern && id != tri::ArpRepeat) return kAudioUnitErr_InvalidProperty;
        if (!strings) return noErr;
        auto values = CFArrayCreateMutable(nullptr,0,&kCFTypeArrayCallBacks);
        auto append=[&](const char* value) {
            CFStringRef s = CFStringCreateWithCString(nullptr,value,kCFStringEncodingUTF8);
            CFArrayAppendValue(values,s);
            CFRelease(s);
        };
        if (wave) for (auto name : tri::waveNames) append(name);
        else if (id == tri::Pattern) for (auto name : tri::patternNames) append(name);
        else for (auto name : {"1","2","3","4","5","6","7","8"}) append(name);
        *strings = values;
        return noErr;
    }
    OSStatus GetPropertyInfo(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element,
                             UInt32& size,bool& writable) override {
        if (scope == global && element == 0) {
            if (id==kAudioUnitProperty_MIDIOutputCallbackInfo) { size=sizeof(CFArrayRef); writable=false; return noErr; }
            if (id==kAudioUnitProperty_MIDIOutputCallback) { size=sizeof(AUMIDIOutputCallbackStruct); writable=true; return noErr; }
            if (id==ternaryAU::processing) { size=sizeof(UInt32); writable=true; return noErr; }
            if (id==ternaryAU::localMIDI) { size=sizeof(ternaryAU::LocalMIDI); writable=true; return noErr; }
            if (id==ternaryAU::assets) { size=sizeof(CFDictionaryRef); writable=true; return noErr; }
            if (id==ternaryAU::midiCapture) { size=sizeof(CFDictionaryRef); writable=true; return noErr; }
            if(id==ternaryAU::display) { size=sizeof(ternaryAU::DisplayState); writable=false; return noErr; }
            if (id==ternaryAU::routes || ternaryAU::isLayoutProperty(id)) {
                size = id==ternaryAU::routes ? sizeof(tri::Routing) : sizeof(float);
                writable=true; return noErr;
            }
            if (id == kAudioUnitProperty_ParameterStringFromValue || id == kAudioUnitProperty_ParameterValueFromString) {
                size = id == kAudioUnitProperty_ParameterStringFromValue
                     ? sizeof(AudioUnitParameterStringFromValue) : sizeof(AudioUnitParameterValueFromString);
                writable = false;
                return noErr;
            }
        }
        if (id == kAudioUnitProperty_CocoaUI && scope == global && element == 0) {
            size = sizeof(AudioUnitCocoaViewInfo); writable=false; return noErr;
        }
        return Base::GetPropertyInfo(id,scope,element,size,writable);
    }
    OSStatus GetProperty(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element,void* data) override {
        if(scope==global && element==0 && id==ternaryAU::processing) {
            const UInt32 enabled=engine.processingEnabled()?1:0;
            std::memcpy(data,&enabled,sizeof(enabled));return noErr;
        }
        if (scope==global && element==0 && id==kAudioUnitProperty_MIDIOutputCallbackInfo) {
            const void *names[]={CFSTR("Soundnet MIDI Out")};
            auto streams=CFArrayCreate(nullptr,names,1,&kCFTypeArrayCallBacks);
            std::memcpy(data,&streams,sizeof(streams));
            return noErr;
        }
        if (scope==global && element==0 && id==ternaryAU::assets) {
            auto dictionary=(__bridge_retained CFDictionaryRef)assetStore.current;
            std::memcpy(data,&dictionary,sizeof(dictionary));
            return noErr;
        }
        if(scope==global&&element==0&&id==ternaryAU::midiCapture) {
            std::lock_guard<std::mutex> lock(captureReplyLock);
            auto dictionary=(__bridge_retained CFDictionaryRef)captureReply;
            std::memcpy(data,&dictionary,sizeof(dictionary));
            return noErr;
        }
        if (scope==global && element==0 && id==ternaryAU::display) {
            ternaryAU::DisplayState view;
            view.graph=engine.routing();
            for(size_t i=0;i<view.layout.size();++i) view.layout[i]=layout[i].load(std::memory_order_relaxed);
            std::memcpy(data,&view,sizeof(view)); return noErr;
        }
        if (scope==global && element==0 && id==ternaryAU::routes) {
            auto graph=engine.routing();
            std::memcpy(data,graph.data(),sizeof(graph)); return noErr;
        }
        if (scope==global && element==0 && ternaryAU::isLayoutProperty(id)) {
            const float pos=layout[id-ternaryAU::layoutBase].load(std::memory_order_relaxed);
            std::memcpy(data,&pos,sizeof(pos)); return noErr;
        }
        if (scope == global && element == 0 && id == kAudioUnitProperty_ParameterStringFromValue) {
            auto& display = *static_cast<AudioUnitParameterStringFromValue*>(data);
            if (!isLocalEnvelope(display.inParamID)) return kAudioUnitErr_InvalidParameter;
            const float value = display.inValue ? *display.inValue : GetParameterRT(display.inParamID);
            display.outString = value < 0 ? static_cast<CFStringRef>(CFRetain(CFSTR("Follow"))) : nullptr;
            return noErr;
        }
        if (scope == global && element == 0 && id == kAudioUnitProperty_ParameterValueFromString) {
            auto& input = *static_cast<AudioUnitParameterValueFromString*>(data);
            if (!isLocalEnvelope(input.inParamID) || !input.inString) return kAudioUnitErr_InvalidParameter;
            if (CFStringCompare(input.inString,CFSTR("Follow"),kCFCompareCaseInsensitive)==kCFCompareEqualTo) {
                input.outValue = tri::Follow;
                return noErr;
            }
            char text[64];
            if (!CFStringGetCString(input.inString,text,sizeof(text),kCFStringEncodingUTF8))
                return kAudioUnitErr_InvalidPropertyValue;
            char* end = nullptr;
            const float parsed = std::strtof(text,&end);
            if (end == text || *end || !std::isfinite(parsed)) return kAudioUnitErr_InvalidPropertyValue;
            input.outValue = parsed < 0 ? tri::Follow : std::min(parsed,tri::params[input.inParamID].max);
            return noErr;
        }
        if (id == kAudioUnitProperty_CocoaUI && scope == global && element == 0) {
            auto* view = static_cast<AudioUnitCocoaViewInfo*>(data);
            view->mCocoaAUViewBundleLocation = copyEditorURL();
            if (!view->mCocoaAUViewBundleLocation) return kAudioUnitErr_InvalidProperty;
            view->mCocoaAUViewClass[0] = static_cast<CFStringRef>(CFRetain(CFSTR("TrnyTernaryViewFactory")));
            return noErr;
        }
        return Base::GetProperty(id,scope,element,data);
    }

    OSStatus RemovePropertyValue(AudioUnitPropertyID id,AudioUnitScope scope,AudioUnitElement element) override {
        if (id==kAudioUnitProperty_MIDIOutputCallback && scope==global && element==0) {
            midiDelivery.publish({});
            return noErr;
        }
        return Base::RemovePropertyValue(id,scope,element);
    }

    OSStatus SetProperty(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element,
                         const void* data, UInt32 size) override {
        if(scope==global && element==0 && id==ternaryAU::processing) {
            if(size!=sizeof(UInt32))return kAudioUnitErr_InvalidPropertyValue;
            UInt32 enabled=0;std::memcpy(&enabled,data,sizeof(enabled));
            if(enabled>1)return kAudioUnitErr_InvalidPropertyValue;
            if(!enabled)discardMidiRequested.store(true,std::memory_order_release);
            engine.setProcessingEnabled(enabled!=0);
            PropertyChanged(ternaryAU::processing,global,0);
            return noErr;
        }
        if (scope==global && element==0 && id==kAudioUnitProperty_MIDIOutputCallback) {
            if (size!=sizeof(AUMIDIOutputCallbackStruct)) return kAudioUnitErr_InvalidPropertyValue;
            AUMIDIOutputCallbackStruct callback{};
            std::memcpy(&callback,data,sizeof(callback));
            midiDelivery.publish(callback);
            return noErr;
        }
        if (scope==global && element==0 && id==ternaryAU::localMIDI) {
            if (size!=sizeof(ternaryAU::LocalMIDI)) return kAudioUnitErr_InvalidPropertyValue;
            ternaryAU::LocalMIDI event{};
            std::memcpy(&event,data,sizeof(event));
            return localEvent(event);
        }
        if (scope==global && element==0 && id==ternaryAU::assets) {
            if (size!=sizeof(CFDictionaryRef)) return kAudioUnitErr_InvalidPropertyValue;
            CFDictionaryRef dictionary=nullptr;
            std::memcpy(&dictionary,data,sizeof(dictionary));
            if (!assetStore.replace((__bridge NSDictionary *)dictionary,engine))
                return kAudioUnitErr_InvalidPropertyValue;
            PropertyChanged(kAudioUnitProperty_ClassInfo,global,0);
            return noErr;
        }
        if(scope==global&&element==0&&id==ternaryAU::midiCapture) {
            if(size!=sizeof(CFDictionaryRef))return kAudioUnitErr_InvalidPropertyValue;
            CFDictionaryRef raw=nullptr;
            std::memcpy(&raw,data,sizeof(raw));
            NSDictionary *command=(__bridge NSDictionary *)raw;
            if(![command isKindOfClass:NSDictionary.class]||
               ![command[@"request"] isKindOfClass:NSString.class])return kAudioUnitErr_InvalidPropertyValue;
            std::lock_guard<std::mutex> lock(captureReplyLock);
            NSMutableDictionary *reply=[triassets::midiCaptureCommand(engine,command) mutableCopy];
            reply[@"request"]=command[@"request"];
            captureReply=reply;
            return noErr;
        }
        if (scope==global && element==0 && id==ternaryAU::routes) {
            tri::Routing graph{};
            if(size!=sizeof(graph)) return kAudioUnitErr_InvalidPropertyValue;
            std::memcpy(graph.data(),data,sizeof(graph));
            if(!engine.setRouting(graph)) return kAudioUnitErr_InvalidPropertyValue;
            PropertyChanged(kAudioUnitProperty_ClassInfo, global, 0);
            return noErr;
        }
        if (scope==global && element==0 && ternaryAU::isLayoutProperty(id)) {
            if(size!=sizeof(float)) return kAudioUnitErr_InvalidPropertyValue;
            const int index=static_cast<int>(id-ternaryAU::layoutBase);
            float pos;
            std::memcpy(&pos,data,sizeof(pos));
            if (!tri::layout::valid(tri::layout::first+index,pos)) return kAudioUnitErr_InvalidPropertyValue;
            layout[index].store(pos,std::memory_order_relaxed);
            PropertyChanged(kAudioUnitProperty_ClassInfo, global, 0);
            return noErr;
        }
        return Base::SetProperty(id,scope,element,data,size);
    }

    OSStatus MIDIEvent(UInt32 status, UInt32 a, UInt32 b, UInt32 frame) override {
        const uint64_t generation=engine.processingGeneration();
        if(!engine.processingEnabled())return noErr;
        if (!queue.push({frame,0,uint8_t(status),uint8_t(a&127),uint8_t(b&127),false,generation}))
            overflow.store(true,std::memory_order_release);
        return noErr;
    }
    OSStatus StartNote(MusicDeviceInstrumentID,MusicDeviceGroupID group,NoteInstanceID* instance,
                       UInt32 frame,const MusicDeviceNoteParams& p) override {
        const auto note = static_cast<UInt32>(std::clamp(p.mPitch,0.f,127.f));
        if (instance) *instance=note;
        return MIDIEvent(0x90|(group&15),note,static_cast<UInt32>(std::clamp(p.mVelocity,0.f,127.f)),frame);
    }
    OSStatus StopNote(MusicDeviceGroupID group,NoteInstanceID note,UInt32 frame) override {
        return MIDIEvent(0x80|(group&15),note,0,frame);
    }

    OSStatus Render(AudioUnitRenderActionFlags& flags,const AudioTimeStamp& ts,UInt32 frames) override {
        const float* inL=nullptr;
        const float* inR=nullptr;
        auto& input = Input(0);
        if (input.IsActive()) {
            auto inputFlags = flags;
            const auto result=PullInput(0,inputFlags,ts,frames);
            if (result && result != kAudioUnitErr_NoConnection) return result;
            if (!result && !(inputFlags & kAudioUnitRenderAction_OutputIsSilence)) {
                auto& buffers = input.GetBufferList();
                inL = static_cast<const float*>(buffers.mBuffers[0].mData);
                inR = buffers.mNumberBuffers > 1 ? static_cast<const float*>(buffers.mBuffers[1].mData) : inL;
            }
        }
        auto& out=Output(0).GetBufferList();
        if (out.mNumberBuffers != 2) return kAudioUnitErr_FormatNotSupported;
        auto* left=static_cast<float*>(out.mBuffers[0].mData);
        auto* right=static_cast<float*>(out.mBuffers[1].mData);
        for(int i=0;i<tri::ParamCount;++i) if(!tri::routeParam(i)) engine.set(i,GetParameterRT(i));
        Float64 beat=0, bpm=0;
        const bool hasBeat=CallHostBeatAndTempo(&beat,&bpm)==noErr;
        engine.setHostTempo(hasBeat ? bpm : 0);
        Boolean playing=false;
        Float64 sampleInTimeline=0;
        auto &callbacks=GetHostCallbackInfo();
        OSStatus transport=callbacks.transportStateProc2
            ? callbacks.transportStateProc2(callbacks.hostUserData,&playing,nullptr,nullptr,&sampleInTimeline,nullptr,nullptr,nullptr)
            : CallHostTransportState(&playing,nullptr,&sampleInTimeline,nullptr,nullptr,nullptr);
        if (hasBeat && std::isfinite(beat) && transport==noErr) engine.setHostTransport(playing,beat);
        else engine.clearHostTransport();
        if(transport==noErr && std::isfinite(sampleInTimeline) && sampleInTimeline>=0 && sampleInTimeline<double(UINT64_MAX))
            engine.setHostTimecode(playing,uint64_t(sampleInTimeline));
        midiDelivery.consume();
        renderStamp=&ts;
        midiOffset=0;
        for (int ch=0;ch<16 && resetChannels;++ch)
            if (resetChannels&(1u<<ch)) {
                emitEvent(this,0,0xb0|ch,121,0);
                emitEvent(this,0,0xb0|ch,123,0);
            }
        resetChannels=0;
        if (overflow.exchange(false)) {
            engine.panic();
            discardEvents();
        }
        if(discardMidiRequested.exchange(false,std::memory_order_acq_rel) || !engine.processingEnabled()) {
            const uint64_t generation=engine.processingGeneration();
            count=size_t(std::remove_if(pending.begin(),pending.begin()+count,
                [generation](const Event& e){return e.generation!=generation;})-pending.begin());
        }
        while (count<pending.size() && queue.pop(pending[count]))
            if(pending[count].generation==engine.processingGeneration())++count;
        std::sort(pending.begin(),pending.begin()+count,[](const Event& a,const Event& b) {
            return a.frame!=b.frame ? a.frame<b.frame : a.order<b.order;
        });

        UInt32 cursor=0;
        auto renderTo=[&](UInt32 end) {
            midiOffset=cursor;
            if(end>cursor) engine.render(inL ? inL+cursor : nullptr,inR ? inR+cursor : nullptr,
                                         left+cursor,right+cursor,static_cast<int>(end-cursor));
            cursor=end;
        };
        size_t i=0;
        while(i<count && pending[i].frame<frames) {
            const Event& e=pending[i++];
            renderTo(e.frame);
            midiOffset=e.frame;
            if(e.generation!=engine.processingGeneration())continue;
            if (e.local) engine.midi(e.status,e.a,e.b);
            else engine.dawMidi(e.status,e.a,e.b);
        }
        renderTo(frames);
        renderStamp=nullptr;
        for(size_t j=i;j<count;++j) { pending[j-i]=pending[j]; pending[j-i].frame-=frames; }
        count-=i;
        flags &= ~kAudioUnitRenderAction_OutputIsSilence;
        return noErr;
    }
};

AUSDK_COMPONENT_ENTRY(ausdk::AUMusicDeviceFactory,TernaryUnit)
