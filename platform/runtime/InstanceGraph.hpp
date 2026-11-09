#pragma once
#include "../../src/DspNodes.hpp"
#include "../../src/AudioSample.hpp"
#include "../../src/VoiceArranger.hpp"
#include <cmath>
#include <memory>

namespace tri {
class InstanceGraph {
    static constexpr int quantum=128, eventCapacity=4096;
    using Event=KernelMidiEvent;
    using Trigger=KernelTriggerEvent;
    struct Pending { uint64_t due; bool start; };
    struct Note { uint8_t velocity=0; uint64_t token=0; };
    using Notes=std::array<Note,2048>;
    struct NotePort {
        std::array<Event,eventCapacity> events{};
        int eventCount=0;
        bool overflow=false;
        uint64_t serial=0,beforeSerial=0;
        Notes before{},after{};
        void begin() {
            bool lostEvents=overflow;
            before=after;beforeSerial=serial;eventCount=0;overflow=false;
            // A full queue can drop releases, including those from engine panic.
            // Clear exactly the notes downstream received before resuming.
            if(lostEvents)for(int k=0;k<2048;++k)if(after[k].velocity)
                emit({0,uint8_t(0x80|k%16),uint8_t(k/16),0});
        }
        void clear() {
            before={};after={};serial=beforeSerial=0;eventCount=0;overflow=false;
        }
        void emit(Event e) {
            if(eventCount==eventCapacity) {overflow=true;return;}
            events[eventCount++]=e;
            int type=e.status&0xf0,k=e.a*16+(e.status&15);
            if(type==0x90 && e.b) after[k]={e.b,++serial};
            else if(type==0x80 || type==0x90) after[k]={};
        }
    };
    struct ArrangerState {
        std::array<NotePort,2> otherVoices;
        uint64_t previousToken=0,sourceGeneration=0;
        int sourcePort=0;
        bool retry=false;
    };
    struct SamplerVoice {
        int note=-1;
        double position=0,step=1,direction=1;
        float velocity=0,envelope=0,attackStep=0,releaseStep=0;
        uint64_t age=0;
        bool active=false,releasing=false;
    };
    struct SamplerState {
        std::array<SamplerVoice,16> voices{};
        const AudioSample* sample=nullptr;
        uint64_t generation=0,serial=0;
        int sourcePort=0;
        bool bypassed=false;
    };
    struct AudioClipState {
        const AudioSample* sample=nullptr;
        double position=0;
        float envelope=0;
        bool playing=false,stopping=false,pendingStart=false,bypassed=false;
        int startFrame=0;
    };
    struct ChannelFilterState {
        int channel=0;bool bypassed=false;
    };
    struct QuantizeState {
        std::array<int,2048> mapping{};
        std::array<int,2048> counts{};
        const void* source=nullptr;
        int sourcePort=0;
        int root=-1,scale=-1,mode=-1;
        bool bypassed=false;
        QuantizeState(){mapping.fill(-1);}
    };
    struct MetronomeState {
        bool running=false;
        double phase=0,clickPhase=0;
        float envelope=0;
        int beat=0;
    };
    struct EchoEvent {
        uint64_t due=0;
        uint8_t status=0,a=0,b=0;
        int step=0;
    };
    struct EchoState {
        std::array<EchoEvent,512> queue{};
        int count=0;
        const void* source=nullptr;
        int sourcePort=0;
        float time=.25f,feedback=.5f;
        int repeats=3,pitch=0;
        bool bypassed=false;
    };
    struct TransposeState {
        std::array<int,2048> mapping{};
        const void* source=nullptr;
        int sourcePort=0;
        int semitones=0;
        bool bypassed=false;
    };
    struct RandomState {
        uint64_t state=0x9e3779b97f4a7c15ull;
        float next01() {
            state^=state>>12;state^=state<<25;state^=state>>27;
            return float((state*2685821657736338717ull)>>40)*(1.0f/16777216.0f);
        }
    };
    struct KernelPortBuffers {
        std::array<float,quantum> left{},right{};
        std::unique_ptr<NotePort> notes;
        std::vector<Event> midi;
        std::vector<Trigger> triggers;
        KernelTimecode timecode;
    };
    struct Runtime : NotePort {
        int type;
        uint64_t generation;
        std::atomic<bool> stopRequested{false};
        std::atomic<bool> retirementHandled{false};
        int lastMidiOutChannel=0; // Render-owned: channel actually sent to the host.
        std::unique_ptr<LegacyEngine> engine;
        std::unique_ptr<AudioProcessor> processor;
        std::unique_ptr<NodeKernel> kernel;
        std::vector<KernelPortBuffers> kernelPorts;
        std::vector<KernelInput> kernelInputs;
        std::vector<KernelOutput> kernelOutputs;
        std::vector<std::vector<Trigger>> kernelTriggers;
        std::vector<size_t> kernelTriggerCounts;
        std::vector<std::vector<Event>> kernelMidiInputs;
        std::vector<std::unique_ptr<Notes>> kernelInputHeld;
        std::unique_ptr<ArrangerState> arranger;
        std::unique_ptr<SamplerState> sampler;
        std::unique_ptr<AudioClipState> audioClip;
        std::unique_ptr<QuantizeState> quantizer;
        std::unique_ptr<ChannelFilterState> channelFilter;
        std::unique_ptr<RandomState> random;
        std::unique_ptr<MetronomeState> metronome;
        std::unique_ptr<TransposeState> transpose;
        std::unique_ptr<EchoState> echo;
        std::array<float,quantum> left{},right{},inputL{},inputR{},sidechainL{},sidechainR{};
        Notes incoming{};
        GraphNode saved;
        bool configured=false;
        int seenPulse=0,count=0,renderOffset=0;float delayMs=-1;
        std::array<Pending,eventCapacity> waiting{};
        int waitingSize=0;
        void* triggerContext=nullptr;
        void (*triggerDispatch)(void*,Runtime*,Trigger)=nullptr;
        std::array<Trigger,eventCapacity> controls{};
        int controlSize=0;
        void trigger(Trigger event) {
            event.frame+=renderOffset;
            if(triggerDispatch)triggerDispatch(triggerContext,this,event);
        }
        NotePort& notePort(int index) {
            if(kernel && index>=0 && size_t(index)<kernelPorts.size() && kernelPorts[size_t(index)].notes)
                return *kernelPorts[size_t(index)].notes;
            // Only multi-output nodes (the arranger) own extra note ports. A
            // stale index must not read past the array.
            if(index<=0 || !arranger) return static_cast<NotePort&>(*this);
            const size_t slot=size_t(index-1);
            if(slot>=arranger->otherVoices.size()) return static_cast<NotePort&>(*this);
            return arranger->otherVoices[slot];
        }
        const float* audioLeft(int port=0) const {
            return kernel && port>=0 && size_t(port)<kernelPorts.size()?kernelPorts[size_t(port)].left.data():left.data();
        }
        const float* audioRight(int port=0) const {
            return kernel && port>=0 && size_t(port)<kernelPorts.size()?kernelPorts[size_t(port)].right.data():right.data();
        }
        Runtime(const GraphNode& node,double rate,uint64_t gen):type(node.type),generation(gen) {
            const NodeSpec* descriptor=nodeSpec(type);
            if(descriptor && descriptor->kernelFactory) {
                kernel=descriptor->kernelFactory();
                if(!kernel)throw std::runtime_error("Node kernel factory returned null");
                kernel->prepare(rate,quantum);
                kernelPorts.resize(descriptor->outputs.size());
                kernelInputs.resize(descriptor->inputs.size());
                kernelOutputs.resize(descriptor->outputs.size());
                kernelTriggers.resize(descriptor->inputs.size());
                kernelTriggerCounts.resize(descriptor->inputs.size());
                kernelMidiInputs.resize(descriptor->inputs.size());
                kernelInputHeld.resize(descriptor->inputs.size());
                for(size_t p=0;p<descriptor->inputs.size();++p) {
                    if(descriptor->inputs[p].signal==Signal::Trigger)kernelTriggers[p].resize(eventCapacity);
                    if(descriptor->inputs[p].signal==Signal::Midi) {
                        // A reconnect/configuration change may require one Off
                        // and one On for every held key before this block's full
                        // upstream event queue. Keep that reconciliation lossless.
                        kernelMidiInputs[p].resize(eventCapacity+2*Notes{}.size());
                        kernelInputHeld[p]=std::make_unique<Notes>();
                    }
                }
                for(size_t p=0;p<descriptor->outputs.size();++p) {
                    if(descriptor->outputs[p].signal==Signal::Midi) {
                        kernelPorts[p].notes=std::make_unique<NotePort>();
                        kernelPorts[p].midi.resize(eventCapacity);
                    }
                    if(descriptor->outputs[p].signal==Signal::Trigger)kernelPorts[p].triggers.resize(eventCapacity);
                }
                return;
            }
            if(type==ArrangerNode) {
                arranger=std::make_unique<ArrangerState>();
                return;
            }
            if(type==SamplerNode) {
                sampler=std::make_unique<SamplerState>();
                return;
            }
            if(type==AudioClipNode) {
                audioClip=std::make_unique<AudioClipState>();
                return;
            }
            if(type==AiMusicNode) {
                // Generated audio plays through the same clip path once the
                // asset has been published; the task state only drives the UI.
                audioClip=std::make_unique<AudioClipState>();
                return;
            }
            if(type==QuantizeNode) {
                quantizer=std::make_unique<QuantizeState>();
                return;
            }
            if(type==ChannelFilterNode) {
                channelFilter=std::make_unique<ChannelFilterState>();
                return;
            }
            if(type==CommentNode) return;
            if(type==MetronomeNode) {
                metronome=std::make_unique<MetronomeState>();
                return;
            }
            if(type==TransposeNode) {
                transpose=std::make_unique<TransposeState>();
                transpose->mapping.fill(-1);
                return;
            }
            if(type==EchoNode) {
                echo=std::make_unique<EchoState>();
                return;
            }
            if(type==RandomNode) {
                random=std::make_unique<RandomState>();
                random->state^=gen*0x9e3779b97f4a7c15ull^node.id;
                if(!random->state)random->state=1;
                return;
            }
            if(auto audio=makeAudioProcessor(type)) {
                processor=std::move(audio);
                processor->prepare(rate);
                return;
            }
            auto pulse=node.parameters.find(TriggerPulse);
            if(pulse!=node.parameters.end())seenPulse=int(pulse->second);
            if(type==MixNode || type==InputNode || type==AudioOutputNode ||
               type==MidiOutNode || type>=StartNode)return;
            engine=std::make_unique<LegacyEngine>();
            engine->prepare(rate);
            engine->set(Master,1);engine->set(MidiLocalOnly,1);
            engine->set(Level1,1);engine->set(Level2,0);engine->set(Level3,0);
            if(type!=DelayNode)engine->set(DelayMix,0);
            Routing route{};
            if(type==Osc1Node) {
                route[routeIndex(RouteOsc1In)]=MidiNode;
                route[routeIndex(RouteOutputIn)]=Osc1Node;
            } else if(type==EQNode || type==DelayNode) {
                route[routeIndex(type==EQNode ? RouteEQIn : RouteDelayIn)]=InputNode;
                route[routeIndex(RouteOutputIn)]=type;
            } else {
                int source=MidiNode;
                if(type==ArpNode) { route[routeIndex(RouteArpIn)]=MidiNode;source=ArpNode; }
                if(type==ChordNode) {route[routeIndex(RouteChordIn)]=MidiNode;source=ChordNode;}
                if(type==ModelNode) {route[routeIndex(RouteModelIn)]=MidiNode;source=ModelNode;}
                if(type==VelocityNode) {route[routeIndex(RouteVelocityIn)]=MidiNode;source=VelocityNode;}
                if(type==ProgressNode) source=ProgressNode;
                if(type==ClipNode) source=ClipNode;
                route[routeIndex(RouteMidiOutIn)]=source;
                engine->setMidiOutput(this,[](void* ptr,int frame,uint8_t s,uint8_t a,uint8_t b) {
                    static_cast<Runtime*>(ptr)->emit({frame,s,a,b});
                });
            }
            if(type==ClipNode)engine->setCycleOutput(this,[](void* ptr,int frame) {
                static_cast<Runtime*>(ptr)->trigger({frame,true});
            });
            engine->setRouting(route);
        }
        void configure(const GraphNode& node) {
            if(kernel) {
                KernelConfig config=resolvedKernelConfig(node);
                kernel->configure(config);
                saved=node;configured=true;
                return;
            }
            if(processor) {
                for(int p:nodeParamList(type))processor->set(p,graphParamValue(node,p));
                saved=node;configured=true;
                return;
            }
            if(engine) {
                for(int p=0;p<ParamCount;++p) {
                    if(routeParam(p))continue;
                    auto next=node.parameters.find(p);
                    auto previous=saved.parameters.find(p);
                    if(next!=node.parameters.end() && (!configured || previous==saved.parameters.end() || previous->second!=next->second)) engine->set(p,next->second);
                    else if(next==node.parameters.end() && previous!=saved.parameters.end())engine->set(p,params[p].def);
                }
                bool clipChanged=node.clip.lengthBeats!=saved.clip.lengthBeats || node.clip.events.size()!=saved.clip.events.size();
                if(!clipChanged)for(size_t i=0;i<node.clip.events.size();++i) {
                    auto a=node.clip.events[i],b=saved.clip.events[i];
                    if(a.beat!=b.beat || a.status!=b.status || a.data1!=b.data1 || a.data2!=b.data2){clipChanged=true;break;}
                }
                if(clipChanged || !configured)engine->setClip(node.clip);
                if(node.model.pitches!=saved.model.pitches || !configured)engine->setModel(node.model);
            }
            saved=node;configured=true;
        }
    };
    struct Entry {
        std::shared_ptr<Runtime> state;
        const NodeSpec* spec=nullptr;
        // One link per declared input port; the port order and meaning come
        // from NodeSpec, never from a fixed array position.
        std::vector<PortLink> inputs;
        std::vector<std::vector<PortLink>> triggerInputs;
        PortLink mainNote{-1,-1},sidechain{-1,-1};
        bool outputSink=false;
        std::array<ArrangerVoice,3> voices;
        float gain=1,bpm=112,wait=1;
        int pulse=0,count=4;
        bool bypass=false;
        std::shared_ptr<const AudioSample> sample;
        float samplerRoot=60,samplerGain=.8f,samplerAttack=.005f,samplerRelease=.12f;
        float samplerStart=0,samplerEnd=1,samplerVelocity=1,samplerXfade=.005f;
        bool samplerLoop=false,samplerReverse=false;
        float audioClipGain=.9f,audioClipStart=0,audioClipEnd=1,audioClipFade=.01f,audioClipPitch=0,audioClipXfade=.005f;
        bool audioClipLoop=false,audioClipReverse=false;
        int quantizeRoot=0,quantizeScale=0,quantizeMode=0;
        float randomChance=.5f;
        float metronomeBPM=120,metronomeLevel=.4f,metronomeAccent=1;
        int metronomeBeats=4;
        int transposeSemitones=0;
        float echoTime=.25f,echoFeedback=.5f;
        int midiOutChannel=0;
        int channelFilter=0;
        float outputWidth=1;
        bool delaySync=false;int delayDivision=1;
        bool tremoloSync=false;int tremoloDivision=0;
        int echoRepeats=3,echoPitch=0;
        int inputSource(int port) const {
            return port>=0&&size_t(port)<inputs.size()?inputs[size_t(port)].node:-1;
        }
    };
    struct Plan { std::vector<Entry> entries; std::vector<std::shared_ptr<Runtime>> retired; uint64_t projectEpoch=0; };
    RealtimeAsset<Plan> plans;
    std::map<InstanceID,std::shared_ptr<Runtime>> controlNodes;
    // Carry releases across coalesced edits until the audio thread acknowledges
    // them. Otherwise removing a sink then publishing again loses its Note Offs.
    std::map<uint64_t,std::shared_ptr<Runtime>> controlRetired;
    uint64_t nextGeneration=1;
    std::string controlProject;
    uint64_t controlProjectEpoch=1,renderProjectEpoch=0;
    std::mutex edits;
    double rate=48000;
    std::atomic<double> bpm{0},beat{0};
    std::atomic<bool> transport{false},playing{false};
    std::atomic<bool> active{false},panicRequested{false};
    std::atomic<bool> processing{true},stopResetRequested{false};
    std::atomic<uint64_t> stopGeneration{0};
    struct QueuedMidi {Event event{};uint64_t generation=0;};
    std::array<QueuedMidi,eventCapacity> local{},daw{};
    int localCount=0,dawCount=0;
    std::array<Event,eventCapacity> pendingLocal{},pendingDaw{};
    int pendingLocalCount=0,pendingDawCount=0;
    std::mutex midiLock;
    std::atomic<bool> inputOverflow{false};
    void* outputContext=nullptr;
    void (*output)(void*,int,uint8_t,uint8_t,uint8_t)=nullptr;
    int outputBase=0;
    const Plan* renderingPlan=nullptr;
    Runtime* renderingNode=nullptr;
    uint64_t samplePosition=0,timelinePosition=0;
    uint64_t timeGeneration=1;
    std::atomic<uint64_t> seekRequested{UINT64_MAX};
    std::atomic<uint64_t> hostFrameRequested{UINT64_MAX};
    std::atomic<uint64_t> timecodeSerial{0},timecodeFrame{0},timecodeGeneration{1};
    std::atomic<double> timecodeRate{48000};
    std::atomic<bool> timecodePlaying{false};
    void publishTimecode(bool running) {
        timecodeSerial.fetch_add(1,std::memory_order_acq_rel);
        timecodeFrame.store(timelinePosition,std::memory_order_relaxed);
        timecodeGeneration.store(timeGeneration,std::memory_order_relaxed);
        timecodeRate.store(rate,std::memory_order_relaxed);
        timecodePlaying.store(running,std::memory_order_relaxed);
        timecodeSerial.fetch_add(1,std::memory_order_release);
    }
    // Fixed capacity: the audio thread must never allocate. Overflow is counted
    // and reported through the same path as an event-queue overflow.
    // Ring FIFO with a head/tail cursor: pushes during processing land after
    // the frame boundary read this block, so nothing is copied and no event is
    // ever delivered twice.
    // A deferred event must survive a topology publish between blocks, so it
    // carries stable instance IDs instead of plan indices or runtime pointers.
    struct DeferredTrigger {
        size_t index=0;
        Trigger event{};
        bool redispatch=false;
        InstanceID sourceId=0,targetId=0;
        int sourcePort=-1;
    };
    std::array<DeferredTrigger,eventCapacity> deferredSlots{};
    size_t deferredHead=0,deferredTail=0;
    uint32_t deferredOverflow=0;
    std::atomic<uint64_t> kernelFaults{0};
    static constexpr size_t dispatchDepthLimit=64;
    std::array<Runtime*,256> dispatchStack{};
    size_t dispatchSize=0;
    // A MIDI output may force its channel. Note-offs must use the same channel
    // as the note-ons, otherwise the host keeps the original note sounding.
    static uint8_t outputChannel(int configured,int inputKey) {
        const int channel=configured>0?((configured-1)&15):(inputKey%16);
        return uint8_t(channel);
    }
    static PortLink linkByRole(const PlanNode& plan,Signal signal,PortRole role) {
        if(!plan.spec)return PortLink{};
        for(size_t p=0;p<plan.spec->inputs.size();++p)
            if(plan.spec->inputs[p].signal==signal && plan.spec->inputs[p].role==role)
                return plan.inputs[p];
        return PortLink{};
    }
    // Feedback queue exhausted. Dropping the backlog is preferable to growing
    // it on the audio thread; the node event counters still report the loss and
    // sounding notes keep their own release path.
    void clearDeferredEvents() {
        deferredHead=deferredTail=0;
        ++deferredOverflow;
    }
    void deliver(Runtime& target,const Entry& entry,Trigger event,int inputPort=-1) {
        if(target.kernel) {
            if(inputPort>=0 && size_t(inputPort)<target.kernelTriggers.size()) {
                auto& count=target.kernelTriggerCounts[size_t(inputPort)];
                auto& queue=target.kernelTriggers[size_t(inputPort)];
                if(count<queue.size())queue[count++]=event;else target.overflow=true;
            }
            return;
        }
        if(target.type==StartNode || target.type==StopNode)
            target.trigger({event.frame,target.type==StartNode});
        else if(target.type==RandomNode) {
            if(target.random && target.random->next01()<entry.randomChance)
                dispatch(&target,event);
        } else if(target.type==CountNode) {
            if(++target.count>=entry.count){target.count=0;target.trigger(event);}
        } else if(target.type==WaitNode) {
            if(target.waitingSize<eventCapacity)
                target.waiting[target.waitingSize++]={samplePosition+uint64_t(event.frame)+uint64_t(entry.wait*rate),event.start};
            else target.overflow=true;
        } else if(target.type==MetronomeNode) {
            auto& state=*target.metronome;
            if(event.start){state.running=true;state.phase=0;state.beat=0;state.envelope=1;state.clickPhase=0;}
            else state.running=false;
        } else if(target.type==AudioClipNode) {
            auto& clip=*target.audioClip;
            if(event.start) {clip.pendingStart=true;clip.startFrame=event.frame;clip.stopping=false;}
            else {clip.pendingStart=false;if(clip.playing)clip.stopping=true;}
        } else if(target.type==AiMusicNode) {
            // Generation belongs to TaskService. Graph triggers only play/stop.
            auto& clip=*target.audioClip;
            if(event.start) {clip.pendingStart=true;clip.startFrame=event.frame;clip.stopping=false;}
            else {clip.pendingStart=false;if(clip.playing)clip.stopping=true;}
        } else if(&target==renderingNode) {
            int p=target.type==ClipNode?ClipRun:target.type==ProgressNode?ProgressRun:ArpOn;
            target.engine->set(p,event.start?1:0);
        } else if(target.controlSize<eventCapacity)target.controls[target.controlSize++]=event;
        else target.overflow=true;
    }
    // Causal dispatch. Trigger-emitting nodes (start/stop/count/wait/random) call
    // back into dispatch through their own Trigger, which carries the converted
    // polarity. The stack only bounds that chain: a re-entered node receives the
    // event but does not recurse, and pathological chains stop instead of
    // overflowing the audio thread.
    void enqueueDeferred(size_t index,Trigger event,Runtime* source,bool redispatch,int sourcePort=-1) {
        if(deferredTail>=deferredSlots.size()) {clearDeferredEvents();return;}
        const InstanceID targetId=(!redispatch&&renderingPlan&&index<renderingPlan->entries.size())
            ? renderingPlan->entries[index].state->saved.id : 0;
        deferredSlots[deferredTail++]={index,event,redispatch,source?source->saved.id:0,targetId,sourcePort};
    }
    void dispatch(Runtime* source,Trigger event,int sourcePort=-1) {
        if(!renderingPlan || !source)return;
        // The chain is too deep to finish this block. Resume from this source at
        // the start of the next block instead of dropping the event.
        if(dispatchSize>=dispatchDepthLimit) {
            enqueueDeferred(0,event,source,true,sourcePort);
            return;
        }
        const auto& entries=renderingPlan->entries;
        dispatchStack[dispatchSize++]=source;
        for(size_t index=0;index<entries.size();++index) {
            const auto& entry=entries[index];
            for(size_t p=0;p<entry.triggerInputs.size();++p) {
                bool matched=false;
                for(const auto& input:entry.triggerInputs[p])
                    if(entries[size_t(input.node)].state.get()==source && (sourcePort<0||sourcePort==input.port)) {matched=true;break;}
                if(!matched)continue;
                // Delivering to a node that is already dispatching is valid: a
                // clip stopping itself at its own loop boundary depends on it.
                // Only the recursion depth is bounded, never the delivery.
                deliver(*entry.state,entry,event,int(p));
            }
        }
        --dispatchSize;
    }









#include "../../nodes/scale-quantize/LegacyRuntime.inc"
#include "../../nodes/channel-filter/LegacyRuntime.inc"
#include "../../nodes/sampler/LegacyRuntime.inc"
#include "../../nodes/audio-clip/LegacyRuntime.inc"
#include "../../nodes/voice-arranger/LegacyRuntime.inc"
#include "../../nodes/metronome/LegacyRuntime.inc"
#include "../../nodes/note-echo/LegacyRuntime.inc"
#include "../../nodes/transpose/LegacyRuntime.inc"
    static bool sameEngineConfig(const GraphNode& a,const GraphNode& b) {
        if(a.parameters!=b.parameters)return false;
        if(a.clip.lengthBeats!=b.clip.lengthBeats || a.clip.events.size()!=b.clip.events.size())return false;
        for(size_t i=0;i<a.clip.events.size();++i) {
            const auto& x=a.clip.events[i];const auto& y=b.clip.events[i];
            if(x.beat!=y.beat || x.status!=y.status || x.data1!=y.data1 || x.data2!=y.data2)return false;
        }
        return a.model.pitches==b.model.pitches;
    }
    // SDK execution bridge. All buffers were allocated by Runtime's constructor
    // on the control thread. Existing nodes and SDK nodes share the same plan.
    void processKernel(const Entry& entry,const std::vector<Entry>& entries,int frames) {
        auto& node=*entry.state;
        for(size_t p=0;p<node.kernelInputs.size();++p) {
            const auto& declaration=entry.spec->inputs[p];
            KernelInput input;input.id=declaration.id;
            const PortLink link=entry.inputs[p];
            Runtime* source=link.connected()?entries[size_t(link.node)].state.get():nullptr;
            if(declaration.signal==Signal::Audio && source) {
                input.left=source->audioLeft(link.port);input.right=source->audioRight(link.port);
            } else if(declaration.signal==Signal::Midi) {
                auto* upstream=source?&source->notePort(link.port):nullptr;
                auto& held=*node.kernelInputHeld[p];
                auto& events=node.kernelMidiInputs[p];
                size_t count=0;
                auto append=[&](Event event){if(count<events.size())events[count++]=event;else kernelFaults.fetch_add(1,std::memory_order_relaxed);};
                for(int k=0;k<2048;++k) {
                    const Note desired=upstream?upstream->before[k]:Note{};
                    if(held[k].velocity==desired.velocity && held[k].token==desired.token)continue;
                    if(held[k].velocity)append({0,uint8_t(0x80|k%16),uint8_t(k/16),0});
                    if(desired.velocity)append({0,uint8_t(0x90|k%16),uint8_t(k/16),desired.velocity});
                }
                if(upstream)for(int i=0;i<upstream->eventCount;++i)append(upstream->events[i]);
                held=upstream?upstream->after:Notes{};
                input.midi=events.data();input.midiCount=count;
            } else if(declaration.signal==Signal::Trigger) {
                input.triggers=node.kernelTriggers[p].data();input.triggerCount=node.kernelTriggerCounts[p];
            } else if(declaration.signal==Signal::Clock && source && source->kernel && link.port>=0 && size_t(link.port)<source->kernelPorts.size()) {
                input.timecode=&source->kernelPorts[size_t(link.port)].timecode;
            }
            node.kernelInputs[p]=input;
        }
        for(size_t p=0;p<node.kernelOutputs.size();++p) {
            auto& storage=node.kernelPorts[p];
            const auto& declaration=entry.spec->outputs[p];
            KernelOutput output;output.id=declaration.id;
            storage.left.fill(0);storage.right.fill(0);
            if(storage.notes)storage.notes->begin();
            if(declaration.signal==Signal::Audio) {output.left=storage.left.data();output.right=storage.right.data();}
            if(declaration.signal==Signal::Midi) {output.midi=storage.midi.data();output.midiCapacity=storage.midi.size();}
            if(declaration.signal==Signal::Trigger) {output.triggers=storage.triggers.data();output.triggerCapacity=storage.triggers.size();}
            if(declaration.signal==Signal::Clock)output.timecode=&storage.timecode;
            node.kernelOutputs[p]=output;
        }
        KernelContext context;
        context.sampleRate=rate;context.frames=frames;
        context.framePosition=timelinePosition;context.timeGeneration=timeGeneration;
        context.playing=processing.load(std::memory_order_relaxed) &&
            (!transport.load(std::memory_order_relaxed)||playing.load(std::memory_order_relaxed));
        context.bypassed=entry.bypass;
        context.inputs=node.kernelInputs.data();context.inputCount=node.kernelInputs.size();
        context.outputs=node.kernelOutputs.data();context.outputCount=node.kernelOutputs.size();
        std::fill(node.kernelTriggerCounts.begin(),node.kernelTriggerCounts.end(),0);
        node.kernel->process(context);
        for(size_t p=0;p<node.kernelOutputs.size();++p) {
            auto& output=node.kernelOutputs[p];auto& storage=node.kernelPorts[p];
            if(output.overflow)kernelFaults.fetch_add(1,std::memory_order_relaxed);
            if(storage.notes) {
                const size_t count=std::min(output.midiCount,storage.midi.size());
                for(size_t i=0;i<count;++i) {
                    const auto event=storage.midi[i];
                    if(event.frame<0||event.frame>=frames||event.status<0x80||event.a>127||event.b>127) {
                        kernelFaults.fetch_add(1,std::memory_order_relaxed);continue;
                    }
                    storage.notes->emit(event);
                }
                if(output.overflow)storage.notes->overflow=true;
            }
            if(entry.spec->outputs[p].signal==Signal::Audio)for(int i=0;i<frames;++i) {
                if(!std::isfinite(storage.left[i])){storage.left[i]=0;kernelFaults.fetch_add(1,std::memory_order_relaxed);}
                if(!std::isfinite(storage.right[i])){storage.right[i]=0;kernelFaults.fetch_add(1,std::memory_order_relaxed);}
            }
            const size_t count=std::min(output.triggerCount,storage.triggers.size());
            for(size_t i=0;i<count;++i) {
                const auto event=storage.triggers[i];
                if(event.frame>=0&&event.frame<frames)dispatch(&node,event,int(p));
                else kernelFaults.fetch_add(1,std::memory_order_relaxed);
            }
        }
    }
public:
    KernelTimecode timecodeSnapshot() const {
        KernelTimecode result;
        for(;;) {
            const uint64_t before=timecodeSerial.load(std::memory_order_acquire);
            if(before&1)continue;
            result={timecodeFrame.load(std::memory_order_relaxed),timecodeGeneration.load(std::memory_order_relaxed),
                    timecodeRate.load(std::memory_order_relaxed),timecodePlaying.load(std::memory_order_relaxed)};
            if(before==timecodeSerial.load(std::memory_order_acquire))return result;
        }
    }
    bool enabled() const {return active.load(std::memory_order_acquire);}
    // Observability for tests and diagnostics: count of feedback events dropped
    // because the deferred queue was full.
    uint32_t droppedFeedbackEvents() const {return deferredOverflow;}
    uint64_t kernelFaultCount() const {return kernelFaults.load(std::memory_order_relaxed);}
    // Publish a complete topology snapshot. The plan is compiled from NodeSpec
    // descriptors; this function only fills kernel-facing entries.
    bool publish(const GraphSpec& spec,bool allowUnknownNodes=false) {
        CompiledGraph compiled;
        if(!compileGraph(spec,compiled,nullptr,allowUnknownNodes))return false;
        std::lock_guard<std::mutex> lock(edits);
        Plan plan;
        if(controlProject!=spec.projectId) {controlProject=spec.projectId;++controlProjectEpoch;}
        plan.projectEpoch=controlProjectEpoch;
        std::map<InstanceID,std::shared_ptr<Runtime>> replacement;
        std::vector<int> entryOf(spec.nodes.size(),-1);
        for(size_t i:compiled.order) {
            const auto& n=spec.nodes[i];
            const PlanNode& node=compiled.nodes[i];
            if(!node.spec)continue;                 // unavailable node: preserved, not executed
            const int type=node.type;
            GraphNode normalized=n;
            normalized.type=type;
            if(normalized.key.empty())normalized.key=node.spec->key;
            auto old=controlNodes.find(n.id);
            bool reuse=old!=controlNodes.end() && old->second->type==type &&
                old->second->saved.nodeVersion==normalized.nodeVersion &&
                old->second->saved.stateVersion==normalized.stateVersion;
            if(reuse && (old->second->engine || old->second->processor || old->second->kernel) &&
               (!sameEngineConfig(old->second->saved,normalized)||old->second->saved.texts!=normalized.texts||
                old->second->saved.sample!=normalized.sample))reuse=false;
            auto runtime=reuse ? old->second : std::make_shared<Runtime>(normalized,rate,nextGeneration++);
            if(!reuse && old!=controlNodes.end() && old->second->stopRequested.load(std::memory_order_acquire))
                runtime->stopRequested.store(true,std::memory_order_release);
            if(!reuse)runtime->configure(normalized);
            Entry entry;
            entry.state=runtime;
            entry.spec=node.spec;
            entry.inputs=node.inputs;
            entry.triggerInputs=node.triggerInputs;
            entry.mainNote=linkByRole(node,Signal::Midi,RoleMain);
            entry.sidechain=linkByRole(node,Signal::Audio,RoleSidechain);
            entry.outputSink=nodeHas(node.spec->capabilities,CapOutputSink);
            entry.gain=graphParamValue(normalized,type==InputNode?InputGain:Master);
            entry.bpm=graphParamValue(normalized,ClockBPM);
            entry.wait=graphParamValue(normalized,WaitSeconds);
            entry.count=int(graphParamValue(normalized,TriggerCount));
            entry.pulse=int(graphParamValue(normalized,TriggerPulse));
            if(type==ArrangerNode)for(int v=0;v<3;++v) {
                int p=ArrHighMin+3*v;
                entry.voices[v]={int(graphParamValue(normalized,p)),int(graphParamValue(normalized,p+1)),
                                 ArrangerTexture(int(graphParamValue(normalized,p+2)))};
            }
            entry.bypass=node.bypass;
            entry.sample=n.sample;
            entry.samplerRoot=graphParamValue(normalized,SamplerRoot);
            entry.samplerGain=graphParamValue(normalized,SamplerGain);
            entry.samplerLoop=graphParamValue(normalized,SamplerLoop)>=.5f;
            entry.samplerAttack=graphParamValue(normalized,SamplerAttack);
            entry.samplerRelease=graphParamValue(normalized,SamplerRelease);
            entry.samplerStart=graphParamValue(normalized,SamplerStart);
            entry.samplerEnd=graphParamValue(normalized,SamplerEnd);
            entry.samplerReverse=graphParamValue(normalized,SamplerReverse)>=.5f;
            entry.samplerVelocity=graphParamValue(normalized,SamplerVelocity);
            entry.samplerXfade=graphParamValue(normalized,SamplerXfade)*.001f;
            entry.audioClipGain=graphParamValue(normalized,AudioClipGain);
            entry.audioClipLoop=graphParamValue(normalized,AudioClipLoop)>=.5f;
            entry.audioClipStart=graphParamValue(normalized,AudioClipStart);
            entry.audioClipEnd=graphParamValue(normalized,AudioClipEnd);
            entry.audioClipFade=graphParamValue(normalized,AudioClipFade);
            entry.audioClipPitch=graphParamValue(normalized,AudioClipPitch);
            entry.audioClipReverse=graphParamValue(normalized,AudioClipReverse)>0.5f;
            entry.audioClipXfade=graphParamValue(normalized,AudioClipXfade)*.001f;
            entry.quantizeRoot=int(graphParamValue(normalized,QuantizeRoot));
            entry.quantizeScale=int(graphParamValue(normalized,QuantizeScale));
            entry.quantizeMode=int(graphParamValue(normalized,QuantizeMode));
            entry.randomChance=graphParamValue(normalized,RandomChance);
            entry.metronomeBPM=graphParamValue(normalized,MetronomeBPM);
            entry.metronomeLevel=graphParamValue(normalized,MetronomeLevel);
            entry.metronomeAccent=graphParamValue(normalized,MetronomeAccent);
            entry.metronomeBeats=int(graphParamValue(normalized,MetronomeBeats));
            entry.transposeSemitones=int(graphParamValue(normalized,TransposeSemitones));
            entry.echoTime=graphParamValue(normalized,EchoTime);
            entry.echoRepeats=int(graphParamValue(normalized,EchoRepeats));
            entry.echoFeedback=graphParamValue(normalized,EchoFeedback);
            entry.echoPitch=int(graphParamValue(normalized,EchoPitch));
            entry.midiOutChannel=int(graphParamValue(normalized,MidiOutChannel));
            entry.channelFilter=int(graphParamValue(normalized,ChannelFilter));
            entry.outputWidth=graphParamValue(normalized,OutputWidth);
            entry.delaySync=graphParamValue(normalized,DelaySync)!=0;
            entry.tremoloSync=graphParamValue(normalized,TremoloSync)!=0;
            entry.tremoloDivision=int(graphParamValue(normalized,TremoloDivision));
            entry.delayDivision=int(graphParamValue(normalized,DelayDivision));
            entryOf[i]=int(plan.entries.size());
            plan.entries.push_back(std::move(entry));
            replacement[n.id]=runtime;
        }
        // Plan links point at document indices; the run plan uses entry indices.
        for(auto& entry:plan.entries) {
            auto remap=[&](PortLink& link) {
                link.node=(link.node>=0&&size_t(link.node)<entryOf.size())?entryOf[size_t(link.node)]:-1;
                if(link.node<0)link.port=-1;
            };
            for(auto& link:entry.inputs)remap(link);
            for(auto& links:entry.triggerInputs) {
                std::vector<PortLink> kept;
                for(auto link:links) {
                    remap(link);
                    if(link.connected())kept.push_back(link);
                }
                links=std::move(kept);
            }
            remap(entry.mainNote);remap(entry.sidechain);
        }
        for(auto it=controlRetired.begin();it!=controlRetired.end();)
            if(it->second->retirementHandled.load(std::memory_order_acquire))it=controlRetired.erase(it);else ++it;
        for(auto& old:controlNodes) {
            auto next=replacement.find(old.first);
            if(old.second->type==MidiOutNode && (next==replacement.end() || next->second!=old.second))
                controlRetired[old.second->generation]=old.second;
        }
        for(const auto& retired:controlRetired)plan.retired.push_back(retired.second);
        plans.publish(plan);
        controlNodes=std::move(replacement);
        active.store(true,std::memory_order_release);
        return true;
    }
    void disable() {active.store(false,std::memory_order_release);}
    void prepare(double sampleRate) {
        rate=sampleRate;
        for(auto& pair:controlNodes) {
            if(pair.second->engine)pair.second->engine->prepare(rate);
            if(pair.second->processor)pair.second->processor->prepare(rate);
            if(pair.second->kernel)pair.second->kernel->prepare(rate,quantum);
        }
        reset();
    }
    void setHostTempo(double tempo) {bpm.store(tempo,std::memory_order_release);}
    void setHostTransport(bool on,double position) {
        playing.store(on,std::memory_order_release);
        beat.store(position,std::memory_order_release);
        transport.store(true,std::memory_order_release);
    }
    void setHostTimecode(bool on,uint64_t frame) {
        playing.store(on,std::memory_order_release);
        transport.store(true,std::memory_order_release);
        hostFrameRequested.store(frame,std::memory_order_release);
    }
    void clearHostTransport() {transport.store(false,std::memory_order_release);}
    void setMidiOutput(void* ctx,void (*fn)(void*,int,uint8_t,uint8_t,uint8_t)) {outputContext=ctx;output=fn;}
    void midi(bool fromDAW,uint8_t s,uint8_t a,uint8_t b) {
        const uint64_t generation=stopGeneration.load(std::memory_order_acquire);
        if(!processing.load(std::memory_order_acquire))return;
        std::unique_lock<std::mutex> lock(midiLock,std::try_to_lock);
        if(!lock.owns_lock()){inputOverflow.store(true,std::memory_order_release);return;}
        if(!processing.load(std::memory_order_acquire) || generation!=stopGeneration.load(std::memory_order_acquire))return;
        auto& count=fromDAW?dawCount:localCount;
        auto& queue=fromDAW?daw:local;
        if(count<eventCapacity)queue[count++]={{0,s,a,b},generation};
        else inputOverflow.store(true,std::memory_order_relaxed);
    }
    void reset(bool preserveQueuedMidi=false,KernelResetReason reason=KernelResetReason::Stop) {
        ++timeGeneration;
        for(auto& entry:plans.current().entries) {
            auto& n=*entry.state;
            if(n.type==MidiOutNode && output)for(int k=0;k<2048;++k)
                if(n.incoming[k].velocity)output(outputContext,0,uint8_t(0x80|outputChannel(n.lastMidiOutChannel,k)),uint8_t(k/16),0);
            if(n.engine)n.engine->reset();
            if(n.processor)n.processor->reset();
            if(n.kernel) {
                n.kernel->reset(reason);
                for(auto& port:n.kernelPorts)if(port.notes)port.notes->clear();
                for(auto& held:n.kernelInputHeld)if(held)*held=Notes{};
                std::fill(n.kernelTriggerCounts.begin(),n.kernelTriggerCounts.end(),0);
            }
            if(n.sampler) {samplerStop(*n.sampler);n.sampler->generation=0;n.sampler->sourcePort=0;n.sampler->bypassed=false;}
            if(n.audioClip) {*n.audioClip=AudioClipState{};}
            if(n.quantizer) {n.quantizer->mapping.fill(-1);n.quantizer->counts.fill(0);n.quantizer->source=nullptr;n.quantizer->sourcePort=0;n.quantizer->root=-1;n.quantizer->scale=-1;n.quantizer->mode=-1;n.quantizer->bypassed=false;}
            if(n.metronome)*n.metronome=MetronomeState{};
            if(n.echo)*n.echo=EchoState{};
            if(n.transpose){*n.transpose=TransposeState{};n.transpose->mapping.fill(-1);}
            if(n.channelFilter)*n.channelFilter=ChannelFilterState{};
            n.clear();n.incoming={};
            if(n.arranger) {
                for(auto& port:n.arranger->otherVoices)port.clear();
                n.arranger->previousToken=0;n.arranger->retry=false;
            }
            n.waitingSize=0;n.controlSize=0;n.count=0;
        }
        deferredHead=deferredTail=0;dispatchSize=0;deferredOverflow=0;
        // controlNodes belongs exclusively to the control thread. Never walk
        // that mutable container here: publish can replace it concurrently.
        pendingLocalCount=pendingDawCount=0;
        inputOverflow.store(false,std::memory_order_relaxed);
        {
            std::unique_lock<std::mutex> lock(midiLock,std::try_to_lock);
            if(!preserveQueuedMidi) {
                if(lock.owns_lock())localCount=dawCount=0;
                else stopGeneration.fetch_add(1,std::memory_order_acq_rel);
            }
        }
    }
    // Only the render thread is allowed to touch live node state. UI/MIDI
    // callers publish a request; the next render block performs the reset.
    void panic() {panicRequested.store(true,std::memory_order_release);}
    void seekTimecode(uint64_t frame) {seekRequested.store(frame,std::memory_order_release);}
    // Control thread only. Independent mailboxes retain every node's request
    // until its next audio boundary; stopping B cannot overwrite a stop for A.
    void requestNodeStop(InstanceID id) {
        std::lock_guard<std::mutex> lock(edits);
        const auto found=controlNodes.find(id);
        if(found!=controlNodes.end())found->second->stopRequested.store(true,std::memory_order_release);
    }
    void setProcessingEnabled(bool enabled) {
        // The reset request survives a rapid Stop -> Play before render runs.
        if(!enabled) {
            processing.store(false,std::memory_order_release);
            stopGeneration.fetch_add(1,std::memory_order_acq_rel);
            stopResetRequested.store(true,std::memory_order_release);
        }
        processing.store(enabled,std::memory_order_release);
    }



    void render(const float* inL,const float* inR,float* outL,float* outR,int frames) {
        if(frames<=0 || !outL || !outR)return;
        const double bpm=this->bpm.load(std::memory_order_acquire);
        const bool transport=this->transport.load(std::memory_order_acquire);
        const bool playing=this->playing.load(std::memory_order_acquire);
        const double beat=this->beat.load(std::memory_order_acquire);
        bool planChanged=plans.consume();
        if(planChanged) {
            if(renderProjectEpoch!=plans.current().projectEpoch) {
                timelinePosition=0;
                if(renderProjectEpoch)reset(false,KernelResetReason::Seek);
                renderProjectEpoch=plans.current().projectEpoch;
            }
            for(auto& node:plans.current().retired) {
                if(node->retirementHandled.exchange(true,std::memory_order_acq_rel))continue;
                if(output)for(int k=0;k<2048;++k) if(node->incoming[k].velocity)
                    output(outputContext,0,uint8_t(0x80|outputChannel(node->lastMidiOutChannel,k)),uint8_t(k/16),0);
                node->incoming={};
            }
            // A node whose note input disappeared (upstream removed, or now an
            // unknown type) must release what it already sent downstream.
            for(const auto& entry:plans.current().entries) {
                auto& state=*entry.state;
                if(state.type!=MidiOutNode)continue;
                if(!entry.mainNote.connected() || state.lastMidiOutChannel!=entry.midiOutChannel) {
                    for(int k=0;k<2048;++k) if(state.incoming[k].velocity) {
                        if(output)output(outputContext,0,
                            uint8_t(0x80|outputChannel(state.lastMidiOutChannel,k)),uint8_t(k/16),0);
                        state.incoming[k]={};
                    }
                }
                state.lastMidiOutChannel=entry.midiOutChannel;
            }
        }
        if(panicRequested.exchange(false,std::memory_order_acq_rel)) {
            for(const auto& entry:plans.current().entries)if(entry.state->engine)entry.state->engine->panic();
            reset(false,KernelResetReason::Panic);
        }
        const uint64_t seek=seekRequested.exchange(UINT64_MAX,std::memory_order_acq_rel);
        if(seek!=UINT64_MAX) {timelinePosition=seek;reset(false,KernelResetReason::Seek);}
        const uint64_t hostFrame=hostFrameRequested.exchange(UINT64_MAX,std::memory_order_acq_rel);
        if(hostFrame!=UINT64_MAX && hostFrame!=timelinePosition) {
            timelinePosition=hostFrame;reset(true,KernelResetReason::Seek);
        }
        const bool stopping=stopResetRequested.exchange(false,std::memory_order_acq_rel);
        if(stopping)reset(true);
        for(const auto& entry:plans.current().entries) {
            if(!entry.state->stopRequested.exchange(false,std::memory_order_acq_rel))continue;
            if(entry.state->kernel) {
                entry.state->kernel->reset(KernelResetReason::Stop);
                for(auto& port:entry.state->kernelPorts)if(port.notes)port.notes->clear();
                for(auto& held:entry.state->kernelInputHeld)if(held)*held=Notes{};
            }
            if(auto* clip=entry.state->audioClip.get()) {
                clip->pendingStart=false;
                if(clip->playing)clip->stopping=true;
            }
        }
        if(!processing.load(std::memory_order_acquire)) {
            // Edits may publish while stopped. Consume their trigger counters
            // without executing nodes so old clicks cannot fire on resume.
            if(planChanged || stopping)for(const auto& entry:plans.current().entries)entry.state->seenPulse=entry.pulse;
            std::fill_n(outL,frames,0.f);std::fill_n(outR,frames,0.f);
            publishTimecode(false);
            return;
        }
        renderingPlan=&plans.current();
        const auto& entries=renderingPlan->entries;
        {
            pendingLocalCount=pendingDawCount=0;
            std::unique_lock<std::mutex> lock(midiLock,std::try_to_lock);
            if(lock.owns_lock()) {
            const uint64_t generation=stopGeneration.load(std::memory_order_acquire);
            pendingLocalCount=pendingDawCount=0;
            for(int i=0;i<localCount;++i)if(local[i].generation==generation)
                pendingLocal[pendingLocalCount++]=local[i].event;
            for(int i=0;i<dawCount;++i)if(daw[i].generation==generation)
                pendingDaw[pendingDawCount++]=daw[i].event;
            localCount=dawCount=0;
            }
        }
        if(inputOverflow.exchange(false,std::memory_order_relaxed))reset();
        for(int offset=0;offset<frames;offset+=quantum) {
            int n=std::min(quantum,frames-offset);
            outputBase=offset;
            for(const auto& entry:entries) {
                auto& state=*entry.state;state.renderOffset=0;
                state.triggerContext=this;
                state.triggerDispatch=[](void* owner,Runtime* source,Trigger event){
                    static_cast<InstanceGraph*>(owner)->dispatch(source,event);
                };
            }
            // Deferred triggers land at the start of the next block. The read
            // boundary is captured first, so anything pushed while processing
            // waits one more block instead of being delivered twice.
            if(deferredHead<deferredTail) {
                const size_t boundary=deferredTail;
                while(deferredHead<boundary) {
                    const DeferredTrigger item=deferredSlots[deferredHead++];
                    Trigger event{item.event.frame,item.event.start};
                    event.frame=0;
                    if(item.redispatch) {
                        Runtime* source=nullptr;
                        for(const auto& candidate:entries)
                            if(candidate.state->saved.id==item.sourceId) {source=candidate.state.get();break;}
                        if(source)dispatch(source,event,item.sourcePort);   // continue a long chain
                        continue;
                    }
                    if(item.index>=entries.size())continue;
                    auto& entry=entries[item.index];
                    if(entry.state->saved.id!=item.targetId)continue;   // node was replaced
                    deliver(*entry.state,entry,event);
                    dispatch(entry.state.get(),event);
                }
                if(deferredHead==deferredTail)deferredHead=deferredTail=0;
            }
            for(const auto& entry:entries) {
                auto& node=*entry.state;
                node.begin();
                if(node.arranger)for(auto& port:node.arranger->otherVoices)port.begin();
                node.left.fill(0);node.right.fill(0);
                int type=node.type;
                // The payload input is discovered from port roles, not from a
                // fixed slot. Trigger, clock and sidechain never land here.
                PortLink primary;Signal primarySignal=Signal::None;
                for(size_t p=0;p<entry.inputs.size()&&p<entry.spec->inputs.size();++p) {
                    if(!entry.inputs[p].connected())continue;
                    const PortSpec& port=entry.spec->inputs[p];
                    // Sidechain, clock and trigger are never the payload: a
                    // compressor with only a detector connected must stay silent.
                    if(port.role==RoleSidechain||port.role==RoleClock||port.role==RoleTrigger||
                       port.role==RoleControl)continue;
                    if(port.signal==Signal::Audio||port.signal==Signal::Midi) {
                        primary=entry.inputs[p];primarySignal=port.signal;break;
                    }
                }
                auto* source=primary.connected()?entries[size_t(primary.node)].state.get():nullptr;
                auto* noteInput=(source&&primarySignal==Signal::Midi)?&source->notePort(primary.port):nullptr;
                const int noteSourcePort=primary.port;
                if(node.kernel) {processKernel(entry,entries,n);continue;}
                if(node.processor) {
                    if(source)for(int i=0;i<n;++i){node.inputL[i]=source->audioLeft(primary.port)[i];node.inputR[i]=source->audioRight(primary.port)[i];}
                    else for(int i=0;i<n;++i){node.inputL[i]=0;node.inputR[i]=0;}
                    if(entry.sidechain.connected()) {
                        const int sideIndex=entry.sidechain.node;
                        auto* side=sideIndex>=0 ? entries[size_t(sideIndex)].state.get() : nullptr;
                        if(side) for(int i=0;i<n;++i){node.sidechainL[i]=side->audioLeft(entry.sidechain.port)[i];node.sidechainR[i]=side->audioRight(entry.sidechain.port)[i];}
                        node.processor->setSidechain(side?node.sidechainL.data():nullptr,
                                                     side?node.sidechainR.data():nullptr,n);
                    }
                    if(type==TremoloNode && node.processor && entry.tremoloSync) {
                        static const double divisions[]={1,2,4,1.5};
                        double hz=std::clamp(double(entry.bpm)/60.0*divisions[std::clamp(entry.tremoloDivision,0,3)],0.1,20.0);
                        node.processor->set(TremoloRate,float(hz));
                    }
                    if(entry.bypass) {
                        for(int i=0;i<n;++i){node.left[i]=node.inputL[i];node.right[i]=node.inputR[i];}
                    } else if(node.processor) {
                        node.processor->render(node.inputL.data(),node.inputR.data(),node.left.data(),node.right.data(),n);
                    }
                    continue;
                }
                if(type==SamplerNode) {
                    bool wasBypassed=node.sampler->bypassed;
                    if(entry.bypass) {
                        if(!wasBypassed)samplerStop(*node.sampler);
                        node.sampler->bypassed=true;
                    } else {
                        node.sampler->bypassed=false;
                        renderSampler(entry,source,noteInput,n,wasBypassed,rate,noteSourcePort);
                    }
                    continue;
                }
                if(type==QuantizeNode) {
                    renderQuantize(entry,node,source,noteInput,noteSourcePort);
                    continue;
                }
                if(type==ChannelFilterNode) {renderChannelFilter(entry,node,source,noteInput,noteSourcePort);continue;}
                if(type==TransposeNode) {renderTranspose(entry,node,source,noteInput,noteSourcePort);continue;}
                if(type==EchoNode) {renderEcho(entry,node,source,noteInput,samplePosition,rate,n,noteSourcePort);continue;}
                if(type==MetronomeNode) {
                    if(entry.bypass){node.metronome->running=false;node.metronome->envelope=0;std::fill(node.left.begin(),node.left.begin()+n,0.f);std::fill(node.right.begin(),node.right.begin()+n,0.f);}
                    else renderMetronome(entry,node,n,rate);
                    continue;
                }
                if(type==AudioClipNode || type==AiMusicNode) {
                    bool wasBypassed=node.audioClip->bypassed;
                    if(entry.bypass) {
                        if(!wasBypassed)*node.audioClip=AudioClipState{};
                        node.audioClip->bypassed=true;
                    } else {
                        node.audioClip->bypassed=false;
                        renderAudioClip(entry,node,n,rate);
                    }
                    continue;
                }
                if(type==ArrangerNode) {
                    renderArranger(entry,source,noteInput,planChanged && offset==0,noteSourcePort);
                    continue;
                }
                if(type==StartNode || type==StopNode) {
                    if(node.seenPulse!=entry.pulse) {
                        node.seenPulse=entry.pulse;node.trigger({0,type==StartNode});
                    }
                    continue;
                }
                if(type==WaitNode || type==CountNode) {
                    int retained=0;
                    for(int j=0;j<node.waitingSize;++j) {
                        auto pending=node.waiting[j];
                        if(pending.due<samplePosition+uint64_t(n))node.trigger({int(pending.due>samplePosition?pending.due-samplePosition:0),pending.start});
                        else node.waiting[retained++]=pending;
                    }
                    node.waitingSize=retained;
                    continue;
                }
                if(type==InputNode) {
                    for(int i=0;i<n;++i) {
                        node.left[i]=inL?inL[offset+i]*entry.gain:0;
                        node.right[i]=inR?inR[offset+i]*entry.gain:node.left[i];
                    }
                    continue;
                }
                if(entry.outputSink) {
                    // The session supplies the destination for the same graph mix.
                    if(source && !entry.bypass)for(int i=0;i<n;++i){
                        float mid=(source->audioLeft(primary.port)[i]+source->audioRight(primary.port)[i])*.5f;
                        float side=(source->audioLeft(primary.port)[i]-source->audioRight(primary.port)[i])*.5f*entry.outputWidth;
                        node.left[i]=(mid+side)*entry.gain;node.right[i]=(mid-side)*entry.gain;
                    }
                    continue;
                }
                if(type==MixNode) {
                    for(const auto& link:entry.inputs) if(link.connected()) {
                        auto& upstream=*entries[size_t(link.node)].state;
                        for(int i=0;i<n;++i){node.left[i]+=upstream.audioLeft(link.port)[i];node.right[i]+=upstream.audioRight(link.port)[i];}
                    }
                    continue;
                }
                // Tempo is node configuration. The shared timeline has no BPM.
                if(node.engine) {
                    const bool timed=entry.spec && entry.spec->timeMode==TimeMode::Timed;
                    node.engine->setHostTempo(entry.bpm);
                    if(timed)node.engine->setHostTransport(
                        !transport || playing,double(timelinePosition)*double(entry.bpm)/(60.0*rate));
                    else node.engine->clearHostTransport();
                }
                if(type==EQNode || type==DelayNode) {
                    if(type==DelayNode && entry.delaySync && node.engine) {
                        static const double beats[4]={1,.5,.25,.75};
                        double ms=60000.0/std::max(1.0,double(entry.bpm))*beats[std::max(0,std::min(3,entry.delayDivision))];
                        ms=std::max(20.0,std::min(1000.0,ms));
                        if(std::fabs(ms-node.delayMs)>.01){node.engine->set(DelayTime,float(ms));node.delayMs=float(ms);}
                    }
                    if(entry.bypass) {
                        if(source)for(int i=0;i<n;++i){node.left[i]=source->audioLeft(primary.port)[i];node.right[i]=source->audioRight(primary.port)[i];}
                    } else {
                        node.engine->render(source?source->audioLeft(primary.port):nullptr,source?source->audioRight(primary.port):nullptr,node.left.data(),node.right.data(),n);
                    }
                    continue;
                }
                auto deliver=[&](Event e) {
                    if(type==MidiOutNode) {
                        uint8_t status=e.status;
                        if(entry.midiOutChannel>0 && (status&0xf0)>=0x80 && (status&0xf0)<=0xe0)
                            status=uint8_t((status&0xf0)|((entry.midiOutChannel-1)&15));
                        if(output)output(outputContext,offset+e.frame,status,e.a,e.b);
                    } else if(node.engine) node.engine->midi(e.status,e.a,e.b);
                };
                auto renderSpan=[&](int begin,int end) {
                    if(!node.engine || end<=begin)return;
                    int previous=node.eventCount;node.renderOffset=begin;
                    renderingNode=&node;
                    node.engine->render(nullptr,nullptr,node.left.data()+begin,node.right.data()+begin,end-begin);
                    renderingNode=nullptr;
                    for(int j=previous;j<node.eventCount;++j)node.events[j].frame+=begin;
                };
                std::sort(node.controls.begin(),node.controls.begin()+node.controlSize,[](auto a,auto b){return a.frame<b.frame;});
                auto applyTrigger=[&](Trigger event) {
                    int param=type==ClipNode?ClipRun:type==ProgressNode?ProgressRun:ArpOn;
                    if(node.engine && type!=VelocityNode)node.engine->set(param,event.start?1:0);
                };
                auto triggeredSpan=[&](int begin,int end,int& controlCursor) {
                    while(controlCursor<node.controlSize && node.controls[controlCursor].frame<end) {
                        auto event=node.controls[controlCursor++];
                        event.frame=std::max(begin,std::min(event.frame,n-1));
                        renderSpan(begin,event.frame);begin=event.frame;
                        applyTrigger(event);
                    }
                    renderSpan(begin,end);
                };
                if(type==MidiNode || type==DawMidiNode) {
                    if(offset==0) {
                        auto& queue=type==MidiNode?pendingLocal:pendingDaw;
                        int count=type==MidiNode?pendingLocalCount:pendingDawCount;
                        for(int j=0;j<count;++j)node.engine->midi(queue[j].status,queue[j].a,queue[j].b);
                    }
                    renderSpan(0,n);
                } else if(type==ClipNode || type==ProgressNode) {
                    int controlCursor=0;triggeredSpan(0,n,controlCursor);
                } else {
                    for(int k=0;k<2048;++k) {
                        Note desired=noteInput?noteInput->before[k]:Note{};
                        auto old=node.incoming[k];
                        if(old.token==desired.token && old.velocity==desired.velocity)continue;
                        if(old.velocity)deliver({0,uint8_t(0x80|(k%16)),uint8_t(k/16),0});
                        if(desired.velocity)deliver({0,uint8_t(0x90|(k%16)),uint8_t(k/16),desired.velocity});
                    }
                    int cursor=0,controlCursor=0;
                    if(noteInput)for(int j=0;j<noteInput->eventCount;++j) {
                        auto event=noteInput->events[j];
                        triggeredSpan(cursor,event.frame,controlCursor);cursor=event.frame;
                        int previous=node.eventCount;
                        deliver(event);
                        for(int k=previous;k<node.eventCount;++k)node.events[k].frame=event.frame;
                    }
                    triggeredSpan(cursor,n,controlCursor);
                    node.incoming=noteInput?noteInput->after:Notes{};
                }
                node.controlSize=0;
                if(node.overflow && node.engine)node.engine->panic();
            }
            samplePosition+=n;
            if(!transport || playing)timelinePosition+=n;
            for(int i=0;i<n;++i) {
                float left=0,right=0;
                for(const auto& entry:entries) {
                    if(entry.outputSink) {left+=entry.state->audioLeft()[i];right+=entry.state->audioRight()[i];}
                }
                outL[offset+i]=left;outR[offset+i]=right;
            }
        }
        if(transport && playing)this->beat.store(beat+frames*(bpm>0?bpm:112)/(60*rate),std::memory_order_release);
        publishTimecode(!transport || playing);
        renderingPlan=nullptr;renderingNode=nullptr;
    }
};
} // namespace tri
