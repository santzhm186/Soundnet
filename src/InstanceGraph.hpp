#pragma once
#include "DspNodes.hpp"
#include "AudioSample.hpp"
#include "VoiceArranger.hpp"
#include <cmath>
#include <memory>

namespace tri {
class InstanceGraph {
    static constexpr int quantum=128, eventCapacity=4096;
    struct Event { int frame; uint8_t status,a,b; };
    struct Trigger { int frame; bool start; };
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
    struct Runtime : NotePort {
        int type;
        uint64_t generation;
        std::unique_ptr<LegacyEngine> engine;
        std::unique_ptr<AudioProcessor> processor;
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
            return index==0 ? static_cast<NotePort&>(*this) : arranger->otherVoices[index-1];
        }
        Runtime(const GraphNode& node,double rate,uint64_t gen):type(node.type),generation(gen) {
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
            if(type==MixNode || type==ClockNode || type==DawClockNode ||
               type==InputNode || type==DawAudioNode || type==OutputNode ||
               type==HardwareOutputNode || type==DawOutputNode || type==MidiOutNode || type>=StartNode)return;
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
        std::array<int,4> inputs{{-1,-1,-1,-1}};
        int noteSourcePort=0;
        std::array<ArrangerVoice,3> voices;
        float gain=1,bpm=112,wait=1;
        int pulse=0,count=4;
        bool hostClock=false,bypass=false;
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
        std::array<bool,4> triggerPorts{};
        std::vector<int> triggerInputs;
    };
    struct Plan { std::vector<Entry> entries; std::vector<std::shared_ptr<Runtime>> retired; };
    RealtimeAsset<Plan> plans;
    std::map<InstanceID,std::shared_ptr<Runtime>> controlNodes;
    uint64_t nextGeneration=1;
    std::mutex edits;
    double rate=48000;
    std::atomic<double> bpm{0},beat{0};
    std::atomic<bool> transport{false},playing{false},hosted{false};
    std::atomic<bool> active{false},panicRequested{false};
    std::array<Event,eventCapacity> local{},daw{};
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
    uint64_t samplePosition=0;
    void dispatch(Runtime* source,Trigger event) {
        if(!renderingPlan)return;
        for(const auto& entry:renderingPlan->entries) {
            auto& target=*entry.state;
            for(int input:entry.triggerInputs) {
                if(renderingPlan->entries[input].state.get()!=source)continue;
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
                } else if(&target==renderingNode) {
                    int p=target.type==ClipNode?ClipRun:target.type==ProgressNode?ProgressRun:ArpOn;
                    target.engine->set(p,event.start?1:0);
                } else if(target.controlSize<eventCapacity)target.controls[target.controlSize++]=event;
                else target.overflow=true;
            }
        }
    }
    static int quantizePitch(int note,int root,int scale,int mode) {
        static constexpr uint16_t masks[5]={
            0x0ab5, // major: C D E F G A B
            0x05ad, // natural minor: C D Eb F G Ab Bb
            0x06ad, // dorian: C D Eb F G A Bb
            0x0295, // major pentatonic: C D E G A
            0x0fff  // chromatic
        };
        if(root<0)root=0;if(root>11)root=11;
        if(scale<0)scale=0;if(scale>4)scale=4;
        const uint16_t mask=masks[scale];
        auto inScale=[&](int pitch) {
            if(pitch<0||pitch>127)return false;
            int relative=(pitch-root)%12;if(relative<0)relative+=12;
            return ((mask>>relative)&1)!=0;
        };
        if(inScale(note))return note;
        if(mode==1) {
            for(int d=1;d<=12;++d)if(inScale(note+d))return note+d;
            return note;
        }
        if(mode==2) {
            for(int d=1;d<=12;++d)if(inScale(note-d))return note-d;
            return note;
        }
        for(int d=1;d<=12;++d) {
            if(inScale(note-d))return note-d;
            if(inScale(note+d))return note+d;
        }
        return note;
    }
    static void renderChannelFilter(const Entry& entry,Runtime& node,Runtime* source,NotePort* input) {
        (void)source;
        auto& state=*node.channelFilter;
        bool force=!input || state.channel!=entry.channelFilter || state.bypassed!=entry.bypass;
        if(force) {
            for(int k=0;k<2048;++k)if(node.after[k].velocity)
                node.emit({0,uint8_t(0x80|(k%16)),uint8_t(k/16),0});
            state.channel=entry.channelFilter;state.bypassed=entry.bypass;
        }
        if(!input)return;
        for(int j=0;j<input->eventCount;++j) {
            const auto event=input->events[j];
            if(entry.bypass || entry.channelFilter==0 || (event.status&15)==entry.channelFilter-1)
                node.emit(event);
        }
        node.controlSize=0;
    }

    static void renderQuantize(const Entry& entry,Runtime& node,Runtime* source,NotePort* input) {
        auto& state=*node.quantizer;
        const bool bypassed=entry.bypass;
        const int root=std::max(0,std::min(11,entry.quantizeRoot));
        const int scale=std::max(0,std::min(4,entry.quantizeScale));
        const int mode=std::max(0,std::min(2,entry.quantizeMode));
        bool force=source!=state.source || entry.noteSourcePort!=state.sourcePort || !source;
        if(root!=state.root || scale!=state.scale || mode!=state.mode || bypassed!=state.bypassed)force=true;
        state.root=root;state.scale=scale;state.mode=mode;state.bypassed=bypassed;
        state.source=source;state.sourcePort=entry.noteSourcePort;
        auto emitOff=[&](int key) {
            node.emit({0,uint8_t(0x80|(key%16)),uint8_t(key/16),0});
        };
        auto noteOn=[&](int pitch,int channel,uint8_t velocity,int frame) {
            const int inputKey=pitch*16+channel;
            const int old=state.mapping[inputKey];
            if(old>=0) {
                if(--state.counts[old]<=0) {state.counts[old]=0;emitOff(old);}
                state.mapping[inputKey]=-1;
            }
            const int outputPitch=bypassed?pitch:quantizePitch(pitch,root,scale,mode);
            const int outputKey=outputPitch*16+channel;
            if(state.counts[outputKey]==0)
                node.emit({frame,uint8_t(0x90|channel),uint8_t(outputPitch),velocity});
            ++state.counts[outputKey];
            state.mapping[inputKey]=outputKey;
        };
        auto syncHeld=[&]() {
            for(int k=0;k<2048;++k)if(node.after[k].velocity)emitOff(k);
            state.mapping.fill(-1);state.counts.fill(0);
            if(input)for(int k=0;k<2048;++k)
                if(input->before[k].velocity)noteOn(k/16,k%16,input->before[k].velocity,0);
        };
        if(force)syncHeld();
        if(input)for(int j=0;j<input->eventCount;++j) {
            const auto event=input->events[j];
            const uint8_t kind=event.status&0xf0;
            if(kind==0x90 && event.b)noteOn(event.a,event.status&15,event.b,event.frame);
            else if(kind==0x80 || kind==0x90) {
                // A note-off can close an older voice even if this pitch was
                // retriggered; the last release wins for that input key.
                const int outputKey=state.mapping[event.a*16+(event.status&15)];
                if(outputKey>=0) {
                    state.mapping[event.a*16+(event.status&15)]=-1;
                    if(--state.counts[outputKey]<=0) {
                        state.counts[outputKey]=0;
                        node.emit({event.frame,uint8_t(0x80|(outputKey%16)),uint8_t(outputKey/16),0});
                    }
                }
            } else node.emit({event.frame,event.status,event.a,event.b});
        }
        node.controlSize=0;
    }
    static void samplerStop(SamplerState& state) {
        for(auto& voice:state.voices){voice.active=false;voice.releasing=false;voice.envelope=0;}
        state.sample=nullptr;
    }
    static void samplerNoteOn(const Entry& entry,SamplerState& state,int note,float velocity,double rate) {
        const AudioSample* sample=entry.sample.get();
        if(!sample || !sample->valid() || note<0 || note>127)return;
        int slot=-1;
        for(int i=0;i<int(state.voices.size());++i)if(!state.voices[i].active){slot=i;break;}
        if(slot<0) {
            slot=0;
            for(int i=1;i<int(state.voices.size());++i)
                if(state.voices[i].age<state.voices[slot].age)slot=i;
        }
        auto& voice=state.voices[slot];
        const size_t frames=sample->frames();
        double start=std::min(std::max(double(entry.samplerStart),0.0),1.0);
        double end=std::min(std::max(double(entry.samplerEnd),0.0),1.0);
        size_t startFrame=size_t(start*double(frames>0?frames-1:0));
        size_t endFrame=size_t(end*double(frames));
        if(endFrame<=startFrame)endFrame=std::min(frames,startFrame+1);
        voice.note=note;
        voice.direction=entry.samplerReverse?-1.0:1.0;
        voice.position=entry.samplerReverse?double(endFrame)-1.0:double(startFrame);
        voice.step=std::pow(2.0,(double(note)-double(entry.samplerRoot))/12.0)*sample->rate/rate;
        const float sensitivity=std::min(std::max(entry.samplerVelocity,0.f),1.f);
        voice.velocity=(1.0f-sensitivity)+sensitivity*std::min(std::max(velocity,0.f),1.f);
        voice.envelope=entry.samplerAttack>0 ? 0.f : 1.f;
        voice.attackStep=entry.samplerAttack>0 ? 1.f/float(entry.samplerAttack*rate) : 1.f;
        voice.releaseStep=entry.samplerRelease>0 ? 1.f/float(entry.samplerRelease*rate) : 1.f;
        voice.age=++state.serial;
        voice.active=true;voice.releasing=false;
    }
    static void renderAudioClip(const Entry& entry,Runtime& node,int frames,double rate) {
        auto& state=*node.audioClip;
        const AudioSample* sample=entry.sample.get();
        if(!sample || !sample->valid()) {state=AudioClipState{};return;}
        if(state.sample!=sample) {
            const bool pending=state.pendingStart;
            const int startFrame=state.startFrame;
            state=AudioClipState{};
            state.sample=sample;
            state.pendingStart=pending;
            state.startFrame=startFrame;
        }
        const size_t sampleFrames=sample->frames();
        const int channels=sample->channels;
        const double start=std::min(std::max(double(entry.audioClipStart),0.0),1.0);
        const double end=std::min(std::max(double(entry.audioClipEnd),0.0),1.0);
        const size_t startFrame=size_t(start*double(sampleFrames>0?sampleFrames-1:0));
        size_t endFrame=size_t(end*double(sampleFrames));
        if(endFrame<=startFrame)endFrame=std::min(sampleFrames,startFrame+1);
        const bool reverse=entry.audioClipReverse;
        const double direction=reverse?-1.0:1.0;
        const double step=double(sample->rate)/rate*std::pow(2.0,double(entry.audioClipPitch)/12.0);
        int first=0;
        if(state.pendingStart) {
            state.pendingStart=false;
            state.position=reverse?double(endFrame)-1.0:double(startFrame);
            state.envelope=entry.audioClipFade>0 ? 0.f : 1.f;
            state.playing=true;state.stopping=false;
            first=std::max(0,std::min(state.startFrame,frames));
            state.startFrame=0;
        }
        if(!state.playing)return;
        if(state.position<double(startFrame) || state.position>=double(endFrame))
            state.position=reverse?double(endFrame)-1.0:double(startFrame);
        const bool loop=entry.audioClipLoop && endFrame>startFrame+1;
        const double xfade=loop ? std::min(double(endFrame-startFrame)*.5,double(entry.audioClipXfade)*rate) : 0.0;
        const float fadeStep=entry.audioClipFade>0 ? 1.f/float(entry.audioClipFade*rate) : 1.f;
        for(int i=first;i<frames && state.playing;++i) {
            if(direction>0 && state.position>=double(endFrame)) {
                if(loop)state.position=double(startFrame);
                else if(state.stopping && state.envelope>0)state.position=std::max(double(startFrame),double(endFrame)-step);
                else {state.playing=false;break;}
            }
            if(direction<0 && state.position<double(startFrame)) {
                if(loop)state.position=std::max(double(startFrame),double(endFrame)-step);
                else if(state.stopping && state.envelope>0)state.position=double(startFrame);
                else {state.playing=false;break;}
            }
            const size_t i0=std::min(size_t(state.position),sampleFrames-1);
            size_t i1=i0+1;
            if(loop) {if(i1>=endFrame)i1=startFrame;}
            else i1=std::min(i1,sampleFrames-1);
            const float fraction=float(state.position-double(i0));
            const float a=sample->samples[i0*size_t(channels)];
            const float b=sample->samples[i1*size_t(channels)];
            const float left=a+(b-a)*fraction;
            float right=left;
            if(channels>1) {
                const float ar=sample->samples[i0*size_t(channels)+1];
                const float br=sample->samples[i1*size_t(channels)+1];
                right=ar+(br-ar)*fraction;
            }
            float loopGain=1.f;
            if(xfade>0.0) {
                const double sinceStart=direction>0 ? state.position-double(startFrame) : double(endFrame)-state.position;
                const double remaining=direction>0 ? double(endFrame)-state.position : state.position-double(startFrame);
                if(sinceStart<xfade)loopGain*=float(std::max(0.0,sinceStart)/xfade);
                if(remaining<xfade)loopGain*=float(std::max(0.0,remaining)/xfade);
            }
            const float amp=state.envelope*loopGain*entry.audioClipGain;
            node.left[i]+=left*amp;node.right[i]+=right*amp;
            if(state.stopping) {
                state.envelope-=fadeStep;
                if(state.envelope<=0){state.envelope=0;state.playing=false;break;}
            } else if(state.envelope<1)state.envelope=std::min(1.f,state.envelope+fadeStep);
            state.position+=step*direction;
            if(!loop && ((direction>0 && state.position>=double(endFrame)) ||
                         (direction<0 && state.position<double(startFrame)))) {
                state.stopping=true;
                if(entry.audioClipFade<=0){state.playing=false;break;}
            }
        }
    }
    static void renderSampler(const Entry& entry,Runtime* source,NotePort* input,int frames,bool force,double rate) {
        auto& node=*entry.state;
        auto& state=*node.sampler;
        const AudioSample* sample=entry.sample.get();
        if(!sample || !sample->valid()) {
            if(state.sample)samplerStop(state);
            return;
        }
        const uint64_t generation=source?source->generation:0;
        const bool reconnected=state.generation!=generation || state.sourcePort!=entry.noteSourcePort;
        bool sync=force || reconnected || state.sample!=sample;
        if(sync) {
            samplerStop(state);
            state.generation=generation;state.sourcePort=entry.noteSourcePort;
            state.sample=sample;state.serial=0;
            if(input)for(int k=0;k<2048;++k)
                if(input->before[k].velocity)
                    samplerNoteOn(entry,state,k/16,float(input->before[k].velocity)/127.f,rate);
        }
        const size_t sampleFrames=sample->frames();
        const int channels=sample->channels;
        double start=std::min(std::max(double(entry.samplerStart),0.0),1.0);
        double end=std::min(std::max(double(entry.samplerEnd),0.0),1.0);
        size_t startFrame=size_t(start*double(sampleFrames>0?sampleFrames-1:0));
        size_t endFrame=size_t(end*double(sampleFrames));
        if(endFrame<=startFrame)endFrame=std::min(sampleFrames,startFrame+1);
        const bool loop=entry.samplerLoop && endFrame>startFrame+1;
        const double xfade=loop ? std::min(double(endFrame-startFrame)*.5,double(entry.samplerXfade)*rate) : 0.0;
        const float outputGain=entry.samplerGain;
        int position=0;
        auto renderTo=[&](int limit) {
            limit=std::max(0,std::min(limit,frames));
            for(;position<limit;++position) {
                for(auto& voice:state.voices) {
                    if(!voice.active)continue;
                    if(voice.direction>0 && voice.position>=double(sampleFrames)){voice.active=false;continue;}
                    if(voice.direction<0 && voice.position<double(startFrame)){voice.active=false;continue;}
                    const size_t i0=size_t(voice.position);
                    size_t i1=i0+1;
                    if(loop) {
                        if(i1>=endFrame)i1=startFrame;
                    } else i1=std::min(i1,sampleFrames-1);
                    const float fraction=float(voice.position-double(i0));
                    auto sampleAt=[&](int channel) {
                        const float a=sample->samples[i0*size_t(channels)+size_t(channel)];
                        const float b=sample->samples[i1*size_t(channels)+size_t(channel)];
                        return a+(b-a)*fraction;
                    };
                    float left=sampleAt(0),right=channels>1?sampleAt(1):left;
                    float loopGain=1.f;
                    if(xfade>0.0) {
                        const double sinceStart=voice.direction>0 ? voice.position-double(startFrame) : double(endFrame)-voice.position;
                        const double remaining=voice.direction>0 ? double(endFrame)-voice.position : voice.position-double(startFrame);
                        if(sinceStart<xfade)loopGain*=float(std::max(0.0,sinceStart)/xfade);
                        if(remaining<xfade)loopGain*=float(std::max(0.0,remaining)/xfade);
                    }
                    const float amp=voice.envelope*loopGain*voice.velocity*outputGain;
                    node.left[position]+=left*amp;
                    node.right[position]+=right*amp;
                    if(voice.releasing) {
                        voice.envelope-=voice.releaseStep;
                        if(voice.envelope<=0){voice.envelope=0;voice.active=false;continue;}
                    } else if(voice.envelope<1) {
                        voice.envelope=std::min(1.f,voice.envelope+voice.attackStep);
                    }
                    voice.position+=voice.step*voice.direction;
                    if(voice.direction>0 && voice.position>=double(endFrame)) {
                        if(loop) {
                            const double length=double(endFrame-startFrame);
                            voice.position=double(startFrame)+std::fmod(voice.position-double(startFrame),length);
                        } else voice.active=false;
                    } else if(voice.direction<0 && voice.position<double(startFrame)) {
                        if(loop) {
                            const double length=double(endFrame-startFrame);
                            voice.position=double(endFrame)-1.0-std::fmod(double(startFrame)-voice.position,length);
                            if(voice.position<double(startFrame))voice.position=double(endFrame)-1.0;
                        } else voice.active=false;
                    }
                }
            }
        };
        if(sync && (!input || !input->eventCount || input->events[0].frame>0))renderTo(0);
        if(input) {
            int cursor=0;
            while(cursor<input->eventCount) {
                const int frame=std::max(0,std::min(input->events[cursor].frame,frames));
                renderTo(frame);
                do {
                    const auto event=input->events[cursor++];
                    const int kind=event.status&0xf0;
                    if(kind==0x80 || (kind==0x90 && !event.b)) {
                        for(auto& voice:state.voices)
                            if(voice.active && !voice.releasing && voice.note==event.a)
                                voice.releasing=true;
                    } else if(kind==0x90) {
                        samplerNoteOn(entry,state,event.a,float(event.b)/127.f,rate);
                    }
                } while(cursor<input->eventCount && input->events[cursor].frame==frame);
            }
        }
        renderTo(frames);
    }
    static void renderArranger(const Entry& entry,Runtime* source,NotePort* input,bool planChanged) {
        auto& node=*entry.state;
        auto& state=*node.arranger;
        uint64_t generation=source ? source->generation : 0;
        bool reconnected=state.sourceGeneration!=generation || state.sourcePort!=entry.noteSourcePort;
        state.sourceGeneration=generation;state.sourcePort=entry.noteSourcePort;
        if(reconnected)state.previousToken=0;
        uint64_t token=input ? input->beforeSerial : 0;
        bool sync=planChanged || state.retry || reconnected;
        if(sync)node.incoming=input ? input->before : Notes{};
        auto revoice=[&](int frame) {
            for(int v=0;v<3;++v) {
                auto& port=node.notePort(v);
                entry.voices[v].voice(node.incoming,port.after,state.previousToken,[&](uint8_t s,uint8_t k,uint8_t vel) {
                    port.emit({frame,s,k,vel});
                });
            }
            state.previousToken=token;
        };
        if(sync && (!input || !input->eventCount || input->events[0].frame>0))revoice(0);
        int cursor=0;
        while(input && cursor<input->eventCount) {
            int frame=input->events[cursor].frame;
            do {
                auto event=input->events[cursor++];
                int kind=event.status&0xf0,key=event.a*16+(event.status&15);
                if(kind==0x90 && event.b)node.incoming[key]={event.b,++token};
                else if(kind==0x80 || kind==0x90)node.incoming[key]={};
            } while(cursor<input->eventCount && input->events[cursor].frame==frame);
            revoice(frame);
        }
        state.retry=node.overflow || state.otherVoices[0].overflow || state.otherVoices[1].overflow;
    }
    static bool sameEngineConfig(const GraphNode& a,const GraphNode& b) {
        if(a.parameters!=b.parameters)return false;
        if(a.clip.lengthBeats!=b.clip.lengthBeats || a.clip.events.size()!=b.clip.events.size())return false;
        for(size_t i=0;i<a.clip.events.size();++i) {
            const auto& x=a.clip.events[i];const auto& y=b.clip.events[i];
            if(x.beat!=y.beat || x.status!=y.status || x.data1!=y.data1 || x.data2!=y.data2)return false;
        }
        return a.model.pitches==b.model.pitches;
    }
public:
    bool enabled() const {return active.load(std::memory_order_acquire);}
    void setHosted(bool v) {hosted.store(v,std::memory_order_release);}
    bool publish(const GraphSpec& spec) {
        std::vector<size_t> order;
        if(!graphOrder(spec,order))return false;
        std::lock_guard<std::mutex> lock(edits);
        Plan plan;
        std::map<InstanceID,std::shared_ptr<Runtime>> replacement;
        std::map<InstanceID,int> indices;
        for(size_t i:order) {
            const auto& n=spec.nodes[i];
            auto old=controlNodes.find(n.id);
            bool reuse=old!=controlNodes.end() && old->second->type==n.type;
            if(reuse && (old->second->engine || old->second->processor) && !sameEngineConfig(old->second->saved,n))reuse=false;
            auto runtime=reuse ? old->second : std::make_shared<Runtime>(n,rate,nextGeneration++);
            if(!reuse)runtime->configure(n);
            Entry entry;
            entry.state=runtime;
            entry.gain=graphParamValue(n,Master);
            entry.bpm=graphParamValue(n,ClockBPM);
            entry.wait=graphParamValue(n,WaitSeconds);entry.count=int(graphParamValue(n,TriggerCount));entry.pulse=int(graphParamValue(n,TriggerPulse));
            if(n.type==ArrangerNode)for(int v=0;v<3;++v) {
                int p=ArrHighMin+3*v;
                entry.voices[v]={int(graphParamValue(n,p)),int(graphParamValue(n,p+1)),
                                 ArrangerTexture(int(graphParamValue(n,p+2)))};
            }
            entry.hostClock=n.type==DawClockNode;
            entry.bypass=n.bypass;
            entry.sample=n.sample;
            entry.samplerRoot=graphParamValue(n,SamplerRoot);
            entry.samplerGain=graphParamValue(n,SamplerGain);
            entry.samplerLoop=graphParamValue(n,SamplerLoop)>=.5f;
            entry.samplerAttack=graphParamValue(n,SamplerAttack);
            entry.samplerRelease=graphParamValue(n,SamplerRelease);
            entry.samplerStart=graphParamValue(n,SamplerStart);
            entry.samplerEnd=graphParamValue(n,SamplerEnd);
            entry.samplerReverse=graphParamValue(n,SamplerReverse)>=.5f;
            entry.samplerVelocity=graphParamValue(n,SamplerVelocity);
            entry.samplerXfade=graphParamValue(n,SamplerXfade)*.001f;
            entry.audioClipGain=graphParamValue(n,AudioClipGain);
            entry.audioClipLoop=graphParamValue(n,AudioClipLoop)>=.5f;
            entry.audioClipStart=graphParamValue(n,AudioClipStart);
            entry.audioClipEnd=graphParamValue(n,AudioClipEnd);
            entry.audioClipFade=graphParamValue(n,AudioClipFade);
                entry.audioClipPitch=graphParamValue(n,AudioClipPitch);
                entry.audioClipReverse=graphParamValue(n,AudioClipReverse)>0.5f;
                entry.audioClipXfade=graphParamValue(n,AudioClipXfade)*.001f;
            entry.quantizeRoot=int(graphParamValue(n,QuantizeRoot));
            entry.quantizeScale=int(graphParamValue(n,QuantizeScale));
            entry.quantizeMode=int(graphParamValue(n,QuantizeMode));
            entry.randomChance=graphParamValue(n,RandomChance);
            entry.metronomeBPM=graphParamValue(n,MetronomeBPM);
            entry.metronomeLevel=graphParamValue(n,MetronomeLevel);
            entry.metronomeAccent=graphParamValue(n,MetronomeAccent);
            entry.metronomeBeats=int(graphParamValue(n,MetronomeBeats));
            entry.transposeSemitones=int(graphParamValue(n,TransposeSemitones));
            entry.echoTime=graphParamValue(n,EchoTime);
            entry.echoRepeats=int(graphParamValue(n,EchoRepeats));
            entry.echoFeedback=graphParamValue(n,EchoFeedback);
            entry.echoPitch=int(graphParamValue(n,EchoPitch));
            entry.midiOutChannel=int(graphParamValue(n,MidiOutChannel));
            entry.channelFilter=int(graphParamValue(n,ChannelFilter));
            entry.outputWidth=graphParamValue(n,OutputWidth);
            entry.delaySync=graphParamValue(n,DelaySync)!=0;
            entry.tremoloSync=graphParamValue(n,TremoloSync)!=0;
            entry.tremoloDivision=int(graphParamValue(n,TremoloDivision));
            entry.delayDivision=int(graphParamValue(n,DelayDivision));
            auto ports=inputTypes(n.type);
            for(size_t p=0;p<ports.size();++p)entry.triggerPorts[p]=ports[p]==Signal::Trigger;

            indices[n.id]=int(plan.entries.size());
            plan.entries.push_back(entry);
            replacement[n.id]=runtime;
        }
        for(auto edge:spec.edges) {
            auto& entry=plan.entries[indices.at(edge.target)];
            if(entry.triggerPorts[edge.port])entry.triggerInputs.push_back(indices.at(edge.source));
            else {
                entry.inputs[edge.port]=indices.at(edge.source);
                if(edge.port==0)entry.noteSourcePort=edge.sourcePort;
            }
        }
        for(auto& old:controlNodes) if(!replacement.count(old.first) && old.second->type==MidiOutNode) plan.retired.push_back(old.second);
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
        }
        reset();
    }
    void setHostTempo(double tempo) {bpm.store(tempo,std::memory_order_release);}
    void setHostTransport(bool on,double position) {
        playing.store(on,std::memory_order_release);
        beat.store(position,std::memory_order_release);
        transport.store(true,std::memory_order_release);
    }
    void clearHostTransport() {transport.store(false,std::memory_order_release);}
    void setMidiOutput(void* ctx,void (*fn)(void*,int,uint8_t,uint8_t,uint8_t)) {outputContext=ctx;output=fn;}
    void midi(bool fromDAW,uint8_t s,uint8_t a,uint8_t b) {
        std::lock_guard<std::mutex> lock(midiLock);
        auto& count=fromDAW?dawCount:localCount;
        auto& queue=fromDAW?daw:local;
        if(count<eventCapacity)queue[count++]={0,s,a,b};
        else inputOverflow.store(true,std::memory_order_relaxed);
    }
    void reset() {
        for(auto& entry:plans.current().entries) {
            auto& n=*entry.state;
            if(n.type==MidiOutNode && output)for(int k=0;k<2048;++k)
                if(n.incoming[k].velocity)output(outputContext,0,0x80|(k%16),k/16,0);
            if(n.engine)n.engine->reset();
            if(n.processor)n.processor->reset();
            if(n.sampler) {samplerStop(*n.sampler);n.sampler->generation=0;n.sampler->sourcePort=0;n.sampler->bypassed=false;}
            if(n.audioClip) {*n.audioClip=AudioClipState{};}
            if(n.quantizer) {n.quantizer->mapping.fill(-1);n.quantizer->counts.fill(0);n.quantizer->source=nullptr;n.quantizer->sourcePort=0;n.quantizer->root=-1;n.quantizer->scale=-1;n.quantizer->mode=-1;n.quantizer->bypassed=false;}
            n.clear();n.incoming={};
            if(n.arranger) {
                for(auto& port:n.arranger->otherVoices)port.clear();
                n.arranger->previousToken=0;n.arranger->retry=false;
            }
            n.waitingSize=0;n.controlSize=0;n.count=0;
        }
        {
            std::lock_guard<std::mutex> lock(midiLock);
            localCount=dawCount=0;
        }
    }
    // Only the render thread is allowed to touch live node state. UI/MIDI
    // callers publish a request; the next render block performs the reset.
    void panic() {panicRequested.store(true,std::memory_order_release);}
    static void renderMetronome(const Entry& entry,Runtime& node,int n,double rate) {
        const double bpm=std::clamp(double(entry.metronomeBPM),40.0,240.0);
        const double step=bpm/(60.0*rate);
        const int beats=std::clamp(entry.metronomeBeats,1,16);
        const float level=std::clamp(entry.metronomeLevel,0.f,1.f);
        const float accent=std::clamp(entry.metronomeAccent,0.f,1.f);
        auto& state=*node.metronome;
        const float decay=std::exp(-1.f/float(0.035*rate));
        for(int i=0;i<n;++i) {
            state.envelope*=decay;
            state.clickPhase+=1800.0/rate;
            if(state.clickPhase>=1.0)state.clickPhase-=1.0;
            if(state.running) {
                state.phase+=step;
                if(state.phase>=1.0) {
                    state.phase-=1.0;
                    state.beat=(state.beat+1)%beats;
                    state.envelope=1;state.clickPhase=0;
                }
            }
            const float amp=state.envelope*level*(state.beat==0?accent:1.f);
            const float v=std::sin(float(2.0*3.14159265358979323846)*float(state.clickPhase))*amp;
            node.left[i]=node.right[i]=v;
        }
    }
    static void renderEcho(const Entry& entry,Runtime& node,Runtime* source,NotePort* input,uint64_t samplePosition,double rate,int n) {
        auto& state=*node.echo;
        const float time=std::clamp(entry.echoTime,.05f,2.f);
        const int repeats=std::clamp(entry.echoRepeats,1,8);
        const float feedback=std::clamp(entry.echoFeedback,0.f,1.f);
        const int pitch=std::clamp(entry.echoPitch,-12,12);
        bool force=source!=state.source || entry.noteSourcePort!=state.sourcePort || !source;
        if(time!=state.time || feedback!=state.feedback || repeats!=state.repeats || pitch!=state.pitch || entry.bypass!=state.bypassed)force=true;
        if(force) {
            for(int k=0;k<2048;++k)if(node.after[k].velocity)node.emit({0,uint8_t(0x80|(k%16)),uint8_t(k/16),0});
            state.count=0;
            state.source=source;state.sourcePort=entry.noteSourcePort;
            state.time=time;state.feedback=feedback;state.repeats=repeats;state.pitch=pitch;state.bypassed=entry.bypass;
            if(entry.bypass && input)for(int k=0;k<2048;++k)if(input->before[k].velocity)
                node.emit({0,uint8_t(0x90|(k%16)),uint8_t(k/16),input->before[k].velocity});
        }
        if(entry.bypass) {
            if(input)for(int j=0;j<input->eventCount;++j)node.emit({input->events[j].frame,input->events[j].status,input->events[j].a,input->events[j].b});
            return;
        }
        auto schedule=[&](uint64_t due,uint8_t status,uint8_t a,uint8_t b,int step) {
            if(state.count>=int(state.queue.size())){node.overflow=true;return;}
            state.queue[state.count++]={due,status,a,b,step};
        };
        auto processDue=[&](uint64_t limit) {
            for(int i=0;i<state.count;) {
                if(state.queue[i].due>=limit){++i;continue;}
                EchoEvent e=state.queue[i];
                state.queue[i]=state.queue[--state.count];
                const int frame=e.due<=samplePosition?0:int(e.due-samplePosition);
                node.emit({frame,e.status,e.a,e.b});
                if((e.status&0xf0)==0x90 && e.b) {
                    schedule(e.due+uint64_t(.15*rate),uint8_t(0x80|(e.status&15)),e.a,0,e.step);
                    if(e.step<state.repeats) {
                        const int nextPitch=std::clamp(int(e.a)+state.pitch,0,127);
                        const uint8_t nextVelocity=uint8_t(std::clamp(int(std::lround(e.b*state.feedback)),1,127));
                        schedule(e.due+uint64_t(state.time*rate),uint8_t(0x90|(e.status&15)),uint8_t(nextPitch),nextVelocity,e.step+1);
                    }
                }
            }
        };
        processDue(samplePosition+uint64_t(n));
        if(input)for(int j=0;j<input->eventCount;++j) {
            const auto event=input->events[j];
            node.emit({event.frame,event.status,event.a,event.b});
            const uint8_t kind=event.status&0xf0;
            if(kind==0x90 && event.b) {
                const int nextPitch=std::clamp(int(event.a)+state.pitch,0,127);
                const uint8_t nextVelocity=uint8_t(std::clamp(int(std::lround(event.b*state.feedback)),1,127));
                schedule(samplePosition+uint64_t(event.frame)+uint64_t(state.time*rate),uint8_t(0x90|(event.status&15)),uint8_t(nextPitch),nextVelocity,1);
            }
        }
    }
    static void renderTranspose(const Entry& entry,Runtime& node,Runtime* source,NotePort* input) {
        auto& state=*node.transpose;
        const int semitones=std::clamp(entry.transposeSemitones,-24,24);
        const bool bypassed=entry.bypass;
        bool force=source!=state.source || entry.noteSourcePort!=state.sourcePort || !source;
        if(semitones!=state.semitones || bypassed!=state.bypassed)force=true;
        state.source=source;state.sourcePort=entry.noteSourcePort;state.semitones=semitones;state.bypassed=bypassed;
        auto emitOff=[&](int key){node.emit({0,uint8_t(0x80|(key%16)),uint8_t(key/16),0});};
        auto mapPitch=[&](int pitch){return std::clamp(pitch+(bypassed?0:semitones),0,127);};
        auto syncHeld=[&](){
            for(int k=0;k<2048;++k)if(node.after[k].velocity)emitOff(k);
            state.mapping.fill(-1);
            if(input)for(int k=0;k<2048;++k)if(input->before[k].velocity){
                const int out=mapPitch(k/16);
                state.mapping[k]=out*16+(k%16);
                node.emit({0,uint8_t(0x90|(k%16)),uint8_t(out),input->before[k].velocity});
            }
        };
        if(force)syncHeld();
        if(input)for(int j=0;j<input->eventCount;++j){
            const auto event=input->events[j];const uint8_t kind=event.status&0xf0;
            const int key=event.a*16+(event.status&15);
            if(kind==0x90 && event.b){
                const int out=mapPitch(event.a);
                state.mapping[key]=out*16+(event.status&15);
                node.emit({event.frame,uint8_t(0x90|(event.status&15)),uint8_t(out),event.b});
            } else if(kind==0x80 || kind==0x90){
                const int mapped=state.mapping[key];
                if(mapped>=0){node.emit({event.frame,uint8_t(0x80|(mapped%16)),uint8_t(mapped/16),0});state.mapping[key]=-1;}
            } else node.emit({event.frame,event.status,event.a,event.b});
        }
    }
    void render(const float* inL,const float* inR,float* outL,float* outR,int frames) {
        if(frames<=0 || !outL || !outR)return;
        const bool hosted=this->hosted.load(std::memory_order_acquire);
        const double bpm=this->bpm.load(std::memory_order_acquire);
        const bool transport=this->transport.load(std::memory_order_acquire);
        const bool playing=this->playing.load(std::memory_order_acquire);
        const double beat=this->beat.load(std::memory_order_acquire);
        bool planChanged=plans.consume();
        if(planChanged) {
            if(output)for(auto& node:plans.current().retired)for(int k=0;k<2048;++k)
                if(node->incoming[k].velocity)output(outputContext,0,0x80|(k%16),k/16,0);
        }
        if(panicRequested.exchange(false,std::memory_order_acq_rel)) {
            for(const auto& entry:plans.current().entries)if(entry.state->engine)entry.state->engine->panic();
            reset();
        }
        renderingPlan=&plans.current();
        const auto& entries=renderingPlan->entries;
        {
            std::lock_guard<std::mutex> lock(midiLock);
            pendingLocalCount=std::min(localCount,eventCapacity);
            pendingDawCount=std::min(dawCount,eventCapacity);
            std::copy_n(local.begin(),pendingLocalCount,pendingLocal.begin());
            std::copy_n(daw.begin(),pendingDawCount,pendingDaw.begin());
            localCount=dawCount=0;
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
            for(const auto& entry:entries) {
                auto& node=*entry.state;
                node.begin();
                if(node.arranger)for(auto& port:node.arranger->otherVoices)port.begin();
                node.left.fill(0);node.right.fill(0);
                int type=node.type;
                int noteSource=entry.inputs[0];
                auto* source=noteSource>=0?entries[noteSource].state.get():nullptr;
                auto* noteInput=source ? &source->notePort(entry.noteSourcePort) : nullptr;
                if(type==NoiseNode) {
                    if(!entry.bypass && node.processor)
                        node.processor->render(nullptr,nullptr,node.left.data(),node.right.data(),n);
                    continue;
                }
                if(type==FilterNode || type==ReverbNode || type==CompressorNode || type==LimiterNode || type==ChorusNode || type==TremoloNode || type==BitcrusherNode || type==PhaserNode || type==GateNode) {
                    if(source)for(int i=0;i<n;++i){node.inputL[i]=source->left[i];node.inputR[i]=source->right[i];}
                    else for(int i=0;i<n;++i){node.inputL[i]=0;node.inputR[i]=0;}
                    if(type==CompressorNode && node.processor) {
                        const int sideIndex=entry.inputs[1];
                        auto* side=sideIndex>=0 ? entries[sideIndex].state.get() : nullptr;
                        if(side) for(int i=0;i<n;++i){node.sidechainL[i]=side->left[i];node.sidechainR[i]=side->right[i];}
                        node.processor->setSidechain(side?node.sidechainL.data():nullptr,
                                                     side?node.sidechainR.data():nullptr,n);
                    }
                    if(type==TremoloNode && node.processor && entry.tremoloSync) {
                        static const double divisions[]={1,2,4,1.5};
                        double hz=std::clamp((bpm>1?bpm:120.0)/60.0*divisions[std::clamp(entry.tremoloDivision,0,3)],0.1,20.0);
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
                        renderSampler(entry,source,noteInput,n,wasBypassed,rate);
                    }
                    continue;
                }
                if(type==QuantizeNode) {
                    renderQuantize(entry,node,source,noteInput);
                    continue;
                }
                if(type==ChannelFilterNode) {renderChannelFilter(entry,node,source,noteInput);continue;}
                if(type==TransposeNode) {renderTranspose(entry,node,source,noteInput);continue;}
                if(type==EchoNode) {renderEcho(entry,node,source,noteInput,samplePosition,rate,n);continue;}
                if(type==MetronomeNode) {
                    if(entry.bypass){node.metronome->running=false;node.metronome->envelope=0;std::fill(node.left.begin(),node.left.begin()+n,0.f);std::fill(node.right.begin(),node.right.begin()+n,0.f);}
                    else renderMetronome(entry,node,n,rate);
                    continue;
                }
                if(type==AudioClipNode) {
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
                    renderArranger(entry,source,noteInput,planChanged && offset==0);
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
                if(type==ClockNode || type==DawClockNode)continue;
                if(type==InputNode || type==DawAudioNode) {
                    bool receives=(type==InputNode && !hosted)||(type==DawAudioNode && hosted);
                    if(receives)for(int i=0;i<n;++i){node.left[i]=inL?inL[offset+i]:0;node.right[i]=inR?inR[offset+i]:node.left[i];}
                    continue;
                }
                if(type==OutputNode || type==HardwareOutputNode || type==DawOutputNode) {
                    bool sends=type==OutputNode || (hosted?type==DawOutputNode:type==HardwareOutputNode);
                    if(source && sends && !entry.bypass)for(int i=0;i<n;++i){
                        float mid=(source->left[i]+source->right[i])*.5f;
                        float side=(source->left[i]-source->right[i])*.5f*entry.outputWidth;
                        node.left[i]=(mid+side)*entry.gain;node.right[i]=(mid-side)*entry.gain;
                    }
                    continue;
                }
                if(type==MixNode) {
                    for(int input:entry.inputs)if(input>=0)for(int i=0;i<n;++i){
                        node.left[i]+=entries[input].state->left[i];node.right[i]+=entries[input].state->right[i];
                    }
                    continue;
                }
                int clockSlot=type==ArpNode || type==VelocityNode?1:0;
                int clockIndex=entry.inputs[clockSlot];
                bool timed=type==ClipNode || type==ProgressNode || type==ArpNode || type==VelocityNode;
                if(node.engine) {
                    bool follows=timed && (clockIndex<0 || entries[clockIndex].hostClock);
                    double tempo=follows?bpm:(clockIndex>=0?entries[clockIndex].bpm:112);
                    node.engine->setHostTempo(tempo);
                    if(transport && follows)node.engine->setHostTransport(playing,beat+offset*(bpm>0?bpm:112)/(60*rate));
                    else node.engine->clearHostTransport();
                }
                if(type==EQNode || type==DelayNode) {
                    if(type==DelayNode && entry.delaySync && node.engine) {
                        static const double beats[4]={1,.5,.25,.75};
                        double ms=60000.0/(bpm>1?bpm:120)*beats[std::max(0,std::min(3,entry.delayDivision))];
                        ms=std::max(20.0,std::min(1000.0,ms));
                        if(std::fabs(ms-node.delayMs)>.01){node.engine->set(DelayTime,float(ms));node.delayMs=float(ms);}
                    }
                    if(entry.bypass) {
                        if(source)for(int i=0;i<n;++i){node.left[i]=source->left[i];node.right[i]=source->right[i];}
                    } else {
                        node.engine->render(source?source->left.data():nullptr,source?source->right.data():nullptr,node.left.data(),node.right.data(),n);
                    }
                    continue;
                }
                auto deliver=[&](Event e) {
                    if(type==MidiOutNode) {
                        uint8_t status=e.status;
                        if(entry.midiOutChannel>0 && (status&0xf0)>=0x80 && (status&0xf0)<=0xe0)
                            status=uint8_t((status&0xf0)|((entry.midiOutChannel-1)&15));
                        if(output)output(outputContext,offset+e.frame,status,e.a,e.b);
                    } else node.engine->midi(e.status,e.a,e.b);
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
            for(int i=0;i<n;++i) {
                float left=0,right=0;
                for(const auto& entry:entries) {
                    int type=entry.state->type;
                    if(type==OutputNode || type==HardwareOutputNode || type==DawOutputNode) {
                        left+=entry.state->left[i];right+=entry.state->right[i];
                    }
                }
                outL[offset+i]=left;outR[offset+i]=right;
            }
        }
        if(transport && playing)this->beat.store(beat+frames*(bpm>0?bpm:112)/(60*rate),std::memory_order_release);
        renderingPlan=nullptr;renderingNode=nullptr;
    }
};
} // namespace tri
