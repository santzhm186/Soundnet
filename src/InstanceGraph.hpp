#pragma once
#include "DspNodes.hpp"
#include "VoiceArranger.hpp"
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
    struct Runtime : NotePort {
        int type;
        uint64_t generation;
        std::unique_ptr<LegacyEngine> engine;
        std::unique_ptr<AudioProcessor> processor;
        std::unique_ptr<ArrangerState> arranger;
        std::array<float,quantum> left{},right{},inputL{},inputR{};
        Notes incoming{};
        GraphNode saved;
        bool configured=false;
        int seenPulse=0,count=0,renderOffset=0;
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
        std::array<bool,4> triggerPorts{};
        std::vector<int> triggerInputs;
    };
    struct Plan { std::vector<Entry> entries; std::vector<std::shared_ptr<Runtime>> retired; };
    RealtimeAsset<Plan> plans;
    std::map<InstanceID,std::shared_ptr<Runtime>> controlNodes;
    uint64_t nextGeneration=1;
    std::mutex edits;
    double rate=48000,bpm=0,beat=0;
    bool transport=false,playing=false,hosted=false;
    std::atomic<bool> active{false},panicRequested{false};
    std::array<Event,eventCapacity> local{},daw{};
    int localCount=0,dawCount=0;
    bool inputOverflow=false;
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
                else if(target.type==CountNode) {
                    if(++target.count>=entry.count){target.count=0;target.trigger(event);}
                } else if(target.type==WaitNode) {
                    if(target.waitingSize<eventCapacity)
                        target.waiting[target.waitingSize++]={samplePosition+uint64_t(event.frame)+uint64_t(entry.wait*rate),event.start};
                    else target.overflow=true;
                } else if(&target==renderingNode) {
                    int p=target.type==ClipNode?ClipRun:target.type==ProgressNode?ProgressRun:ArpOn;
                    target.engine->set(p,event.start?1:0);
                } else if(target.controlSize<eventCapacity)target.controls[target.controlSize++]=event;
                else target.overflow=true;
            }
        }
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
public:
    bool enabled() const {return active.load(std::memory_order_acquire);}
    void setHosted(bool v) {hosted=v;}
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
            auto runtime=old!=controlNodes.end() && old->second->type==n.type ? old->second : std::make_shared<Runtime>(n,rate,nextGeneration++);
            runtime->configure(n);
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
    void setHostTempo(double tempo) {bpm=tempo;}
    void setHostTransport(bool on,double position) {transport=true;playing=on;beat=position;}
    void clearHostTransport() {transport=false;}
    void setMidiOutput(void* ctx,void (*fn)(void*,int,uint8_t,uint8_t,uint8_t)) {outputContext=ctx;output=fn;}
    void midi(bool fromDAW,uint8_t s,uint8_t a,uint8_t b) {
        auto& count=fromDAW?dawCount:localCount;
        auto& queue=fromDAW?daw:local;
        if(count<eventCapacity)queue[count++]={0,s,a,b};
        else inputOverflow=true;
    }
    void reset() {
        for(auto& entry:plans.current().entries) {
            auto& n=*entry.state;
            if(n.type==MidiOutNode && output)for(int k=0;k<2048;++k)
                if(n.incoming[k].velocity)output(outputContext,0,0x80|(k%16),k/16,0);
            if(n.engine)n.engine->reset();
            if(n.processor)n.processor->reset();
            n.clear();n.incoming={};
            if(n.arranger) {
                for(auto& port:n.arranger->otherVoices)port.clear();
                n.arranger->previousToken=0;n.arranger->retry=false;
            }
            n.waitingSize=0;n.controlSize=0;n.count=0;
        }
        localCount=dawCount=0;
    }
    void panic() {panicRequested.store(true,std::memory_order_release);reset();}
    void render(const float* inL,const float* inR,float* outL,float* outR,int frames) {
        if(frames<=0 || !outL || !outR)return;
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
        if(inputOverflow) {panic();inputOverflow=false;}
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
                if(type==FilterNode || type==ReverbNode) {
                    if(source)for(int i=0;i<n;++i){node.inputL[i]=source->left[i];node.inputR[i]=source->right[i];}
                    else for(int i=0;i<n;++i){node.inputL[i]=0;node.inputR[i]=0;}
                    if(entry.bypass) {
                        for(int i=0;i<n;++i){node.left[i]=node.inputL[i];node.right[i]=node.inputR[i];}
                    } else if(node.processor) {
                        node.processor->render(node.inputL.data(),node.inputR.data(),node.left.data(),node.right.data(),n);
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
                    if(source && sends && !entry.bypass)for(int i=0;i<n;++i){node.left[i]=source->left[i]*entry.gain;node.right[i]=source->right[i]*entry.gain;}
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
                    if(entry.bypass) {
                        if(source)for(int i=0;i<n;++i){node.left[i]=source->left[i];node.right[i]=source->right[i];}
                    } else {
                        node.engine->render(source?source->left.data():nullptr,source?source->right.data():nullptr,node.left.data(),node.right.data(),n);
                    }
                    continue;
                }
                auto deliver=[&](Event e) {
                    if(type==MidiOutNode) {
                        if(output)output(outputContext,offset+e.frame,e.status,e.a,e.b);
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
                        auto& queue=type==MidiNode?local:daw;
                        int count=type==MidiNode?localCount:dawCount;
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
        if(transport && playing)beat+=frames*(bpm>0?bpm:112)/(60*rate);
        localCount=dawCount=0;renderingPlan=nullptr;renderingNode=nullptr;
    }
};
} // namespace tri
