#pragma once
// Public descriptor API. Node modules include this without importing the app,
// graph runtime, other node modules, or the legacy synthesizer.
#include "NodeKernel.hpp"
#include "ParamInfo.hpp"
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace tri {
// ---------------------------------------------------------------------------
// Signals
// ---------------------------------------------------------------------------
// `Notes` is kept as an alias so existing call sites read naturally, but the
// contract name is `Midi`.
enum class Signal : uint8_t { None, Audio, Midi, Trigger, Clock, Control, Resource, TaskEvent, Notes=Midi };

inline const char* signalKey(Signal s) {
    switch(s) {
        case Signal::Audio: return "audio";
        case Signal::Midi: return "midi";
        case Signal::Trigger: return "trigger";
        case Signal::Clock: return "clock";
        case Signal::Control: return "control";
        case Signal::Resource: return "resource";
        case Signal::TaskEvent: return "taskEvent";
        default: return "none";
    }
}
inline bool signalFromKey(const char* key,Signal& out) {
    if(!key) return false;
    struct Row { const char* key; Signal signal; };
    static const Row rows[] = {
        {"audio",Signal::Audio},{"midi",Signal::Midi},{"notes",Signal::Midi},{"trigger",Signal::Trigger},
        {"clock",Signal::Clock},{"control",Signal::Control},{"resource",Signal::Resource},
        {"taskEvent",Signal::TaskEvent},{"none",Signal::None}
    };
    for(const auto& row:rows) if(std::strcmp(row.key,key)==0) {out=row.signal;return true;}
    return false;
}

// ---------------------------------------------------------------------------
// Ports
// ---------------------------------------------------------------------------
enum PortRole : uint8_t {
    RoleData=0,      // ordinary payload input/output
    RoleMain,        // the node's primary note/payload input (never positional)
    RoleClock,       // timing input or output
    RoleSidechain,   // detector input
    RoleTrigger,     // causal start/stop style input or output
    RoleControl,     // automation / transport style input
    RoleResource,    // immutable content handle
    RoleEvent,       // task lifecycle event
    RoleOutput       // terminal sink (audio leaves the graph here)
};
enum PortFlags : uint8_t {
    PortMultiSource  = 1<<0,  // several distinct sources; Trigger inputs only
    PortOptional     = 1<<1,  // omit incomplete-input diagnostic when disconnected
    PortFeedback     = 1<<2,  // legacy adapter only; SDK 1 rejects this flag
};
struct PortSpec {
    const char* id;      // stable, persisted in v4 projects
    const char* label;   // display only, may change freely
    Signal signal;
    PortRole role;
    uint8_t flags;
};

// ---------------------------------------------------------------------------
// Execution capabilities and time semantics
// ---------------------------------------------------------------------------
inline constexpr uint32_t CapData          =1u<<0;  // evaluate() from input snapshot
inline constexpr uint32_t CapGenerator     =1u<<1;  // produces output without inputs
inline constexpr uint32_t CapStream        =1u<<2;  // realtime/offline block processing
inline constexpr uint32_t CapTask          =1u<<3;  // background or external task
inline constexpr uint32_t CapMultiOutput   =1u<<4;  // holds state per output port
inline constexpr uint32_t CapNoteSource    =1u<<5;  // emits a MIDI/note payload
inline constexpr uint32_t CapClockSource   =1u<<6;  // provides a time domain position
inline constexpr uint32_t CapOutputSink    =1u<<7;  // summed into the graph output
inline constexpr uint32_t CapTriggerSource =1u<<8;  // emits causal trigger events
inline constexpr uint32_t CapTriggerTarget =1u<<9;  // consumes causal trigger events
inline constexpr uint32_t CapBypassable    =1u<<10;
inline constexpr uint32_t CapAssetClip     =1u<<11;
inline constexpr uint32_t CapAssetAudio    =1u<<12;
inline constexpr uint32_t CapAssetModel    =1u<<13;
inline constexpr uint32_t CapTemporal      =1u<<14; // needs a musical/clock time domain
inline constexpr uint32_t CapSnapshot      =1u<<15; // may publish an immutable result resource

// How a node relates to content time. Ordinary data nodes are TimeMode::None and
// must run without a sample rate, BPM or transport.
enum class TimeMode : uint8_t { None, Content, Timed, Stream };

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------
enum class ParamGroup : uint8_t { General, Tone, Envelope, Playback, Timing, Modulation, Mix, Dynamics, Harmony };
inline const char* paramGroupLabel(ParamGroup group) {
    switch(group) {
        case ParamGroup::Tone:return "音色与音高";
        case ParamGroup::Envelope:return "包络与淡化";
        case ParamGroup::Playback:return "播放与区间";
        case ParamGroup::Timing:return "节奏与同步";
        case ParamGroup::Modulation:return "调制与空间";
        case ParamGroup::Mix:return "电平与混音";
        case ParamGroup::Dynamics:return "动态控制";
        case ParamGroup::Harmony:return "和声与音域";
        default:return "基本参数";
    }
}
struct ParamSpec {
    const ParamInfo* info;
    const char* key;      // stable alias, used by the v4 project format
    bool discrete;        // integral value; validation rejects fractions
    // Optional enumerated labels, in value order starting at 0. The inspector
    // builds its popup from here, so a node never ships two copies of the list.
    const char* const* options=nullptr;
    uint8_t optionCount=0;
    // Presentation metadata only; never changes the stable key or wire order.
    ParamGroup group=ParamGroup::General;
};

// ---------------------------------------------------------------------------
// Node descriptors
// ---------------------------------------------------------------------------
// Text parameters are declared separately from numeric parameters: they have no
// value range and therefore no numeric parameter ID. They persist in the
// node-level `texts` dictionary of a v4 document.
struct TextSpec {
    const char* id;      // stable key, persisted in v4 projects
    const char* label;   // display only
    const char* defaultValue="";
    size_t maxBytes=256*1024;
    bool multiline=false;
    bool readOnly=false;
    bool hidden=false;
};
struct NodeSpec {
    const char* key;
    int type;
    const char* title;
    const char* category;
    uint16_t nodeVersion=1;
    uint8_t stateVersion=1;
    bool visible=true;
    bool instantiable=true;
    uint32_t capabilities=0;
    TimeMode timeMode=TimeMode::None;
    int latencyFrames=0;
    std::vector<PortSpec> inputs;
    std::vector<PortSpec> outputs;
    std::vector<int> params;
    const char* assetKind=nullptr;
    std::vector<TextSpec> texts;
    KernelFactory kernelFactory=nullptr;
    ConfigValidator validateConfig=nullptr;
    EnvironmentRole environmentRole=EnvironmentRole::None;
    unsigned sdkVersion=nodeSDKVersion;
    bool legacyAdapter=false;
};

// Display-only strings belong to their node. Stable IDs and user-authored text
// never pass through this table. Source is normally the Chinese display text.
struct NodeTranslation {
    const char* source;
    const char* en;
};
struct NodeModule {
    NodeSpec spec;
    std::vector<std::pair<int,ParamSpec>> parameters;
    std::vector<NodeTranslation> translations;
};
const std::vector<NodeModule>& nodeModules();
inline const char* nodeTranslation(int type,const char* source,bool english,
                                  const std::vector<NodeModule>& modules=nodeModules()) {
    if(!source)return nullptr;
    for(const auto& module:modules)if(module.spec.type==type) {
        for(const auto& row:module.translations) {
            if(!row.source||!row.en)continue;
            if(std::strcmp(source,row.source)==0||std::strcmp(source,row.en)==0)
                return english?row.en:row.source;
        }
        break;
    }
    return nullptr;
}
inline void registerParameter(std::map<int,ParamSpec>& target,int id,ParamSpec spec) {
    if(!target.emplace(id,spec).second)
        throw std::logic_error("Duplicate node parameter ID: "+std::to_string(id));
}


namespace detail {
inline PortSpec pin(const char* id,const char* label,Signal s,PortRole r=RoleData,uint8_t f=PortOptional) {
    // Every causal trigger input accepts several distinct sources; identical
    // duplicates are rejected by the compiler instead of silently doubling.
    if(r==RoleTrigger) f|=PortMultiSource;
    return PortSpec{id,label,s,r,f};
}
inline PortSpec pout(const char* id,const char* label,Signal s,PortRole r=RoleData) {
    return PortSpec{id,label,s,r,0};
}
}



} // namespace tri
