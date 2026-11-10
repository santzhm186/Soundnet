#pragma once
#include "../GraphSpec.hpp"
#include "RealtimeAsset.hpp"
#include "MidiInputCapture.hpp"
#include "MidiControllerState.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
namespace tri {
class InstanceGraph {
    static constexpr int quantum=128,eventCapacity=4096;
    using Event=KernelMidiEvent;
    using Trigger=KernelTriggerEvent;
    struct Note {uint8_t velocity=0;uint64_t token=0;};
    using Notes=std::array<Note,2048>;
    struct NotePort {
        std::array<Event,eventCapacity> events{};
        size_t count=0;uint64_t serial=0;bool overflow=false;
        Notes before{},after{};
        MidiControllerState controllers;
        void clear(){count=0;before={};after={};controllers={};overflow=false;}
        void emit(Event e){
            if(count>=events.size()){overflow=true;return;}
            events[count++]=e;
            const int kind=e.status&0xf0,k=e.a*16+(e.status&15);
            if(kind==0x90&&e.b)after[k]={e.b,++serial};
            else if(kind==0x80||kind==0x90)after[k]={};
            else {
                if(kind==0xb0&&(e.a==120||e.a>=123))
                    for(int key=e.status&15;key<2048;key+=16)after[key]={};
                controllers.observe(e.status,e.a,e.b);
            }
        }
        void begin(){
            const bool lost=overflow;before=after;count=0;overflow=false;
            if(lost){controllers.release([&](uint8_t s,uint8_t a,uint8_t b){emit({0,s,a,b});});
                for(int k=0;k<2048;++k)if(after[k].velocity)emit({0,uint8_t(0x80|k%16),uint8_t(k/16),0});}
        }
    };
    struct PortBuffers {
        std::array<float,quantum> left{},right{};
        std::unique_ptr<NotePort> notes;
        std::vector<Event> midi;
        std::vector<Trigger> triggers;
        KernelTimecode timecode;
    };
    struct Runtime {
        GraphNode saved;const NodeSpec* spec=nullptr;
        std::shared_ptr<const NodeCatalog> catalog;
        uint64_t generation=0;
        std::unique_ptr<NodeKernel> kernel;
        std::vector<PortBuffers> ports;
        std::vector<KernelInput> inputs;
        std::vector<KernelOutput> outputs;
        std::vector<std::vector<Trigger>> triggers;
        std::vector<size_t> triggerCounts;
        std::vector<std::vector<Event>> midiInputs;
        std::vector<std::unique_ptr<Notes>> held;
        std::vector<MidiControllerState> inputControllers;
        std::vector<uint64_t> inputGenerations;
        std::array<Event,eventCapacity> hostEvents{};
        NotePort hostOutput;
        std::atomic<bool> stopRequested{false},retirementHandled{false};
        Runtime(const GraphNode& node,const NodeSpec* descriptor,double rate,uint64_t gen,
                std::shared_ptr<const NodeCatalog> owner,const Runtime* previous):
            saved(node),spec(descriptor),catalog(std::move(owner)),generation(gen) {
            if(!spec->kernelFactory)return;
            kernel=spec->kernelFactory();if(!kernel)throw std::runtime_error("Node factory returned null");
            kernel->prepare(rate,quantum);
            ports.resize(spec->outputs.size());outputs.resize(ports.size());
            inputs.resize(spec->inputs.size());triggers.resize(inputs.size());triggerCounts.resize(inputs.size());
            midiInputs.resize(inputs.size());held.resize(inputs.size());inputControllers.resize(inputs.size());inputGenerations.resize(inputs.size());
            for(size_t p=0;p<inputs.size();++p){
                if(spec->inputs[p].signal==Signal::Trigger)triggers[p].resize(eventCapacity);
                if(spec->inputs[p].signal==Signal::Midi){midiInputs[p].resize(eventCapacity+8192);held[p]=std::make_unique<Notes>();}}
            for(size_t p=0;p<ports.size();++p){
                if(spec->outputs[p].signal==Signal::Midi){ports[p].notes=std::make_unique<NotePort>();ports[p].midi.resize(eventCapacity);}
                if(spec->outputs[p].signal==Signal::Trigger)ports[p].triggers.resize(eventCapacity);}
            auto config=resolvedKernelConfig(saved);config.instanceID=node.id;
            if(previous){config.hasPreviousConfiguration=true;config.previousParameters=resolvedKernelConfig(previous->saved).parameters;}
            kernel->configure(config);
        }
        const float* left(int p)const{return ports[size_t(p)].left.data();}
        const float* right(int p)const{return ports[size_t(p)].right.data();}
        void reset(KernelResetReason reason){
            if(kernel)kernel->reset(reason);
            for(auto& p:ports)if(p.notes)p.notes->clear();
            for(auto& h:held)if(h)*h={};
            for(auto& c:inputControllers)c={};
            std::fill(inputGenerations.begin(),inputGenerations.end(),0);
            std::fill(triggerCounts.begin(),triggerCounts.end(),0);
        }
    };
    struct Entry {
        std::shared_ptr<Runtime> state;
        const NodeSpec* spec=nullptr;
        std::vector<PortLink> inputs;
        std::vector<std::vector<PortLink>> triggerInputs;
        PortLink mainNote;
        bool bypass=false,outputSink=false;
    };
    struct Plan {
        std::shared_ptr<const NodeCatalog> catalog;
        std::vector<Entry> entries;
        std::map<InstanceID,size_t> byID;
        std::vector<std::shared_ptr<Runtime>> retired;
        uint64_t projectEpoch=0;
    };
    RealtimeAsset<Plan> plans;
    struct CaptureRoute{uint64_t targetGeneration=0,sourceGeneration=0;int port=-1;
        bool operator==(const CaptureRoute& b)const{return targetGeneration==b.targetGeneration&&sourceGeneration==b.sourceGeneration&&port==b.port;}};
    struct CaptureTarget{InstanceID node=0;std::shared_ptr<MidiInputCapture> recording;};
    RealtimeAsset<CaptureTarget> capturePlans;
    CaptureTarget activeCapture;
    std::map<InstanceID,CaptureRoute> captureRoutes;
    uint64_t captureToken=0;
    std::map<InstanceID,std::shared_ptr<Runtime>> controlNodes;
    std::map<uint64_t,std::shared_ptr<Runtime>> controlRetired;
    std::mutex edits,midiLock;
    double rate=48000;
    uint64_t nextGeneration=1,controlProjectEpoch=1,renderProjectEpoch=0;
    std::string controlProject;
    std::atomic<bool> active{false},processing{true},panicRequested{false},stopResetRequested{false};
    std::atomic<bool> transport{false},playing{false},inputOverflow{false};
    std::atomic<uint64_t> stopGeneration{0},seekRequested{UINT64_MAX},hostFrameRequested{UINT64_MAX};
    struct QueuedMidi{Event event;uint64_t generation;};
    std::array<QueuedMidi,eventCapacity> local{},daw{};
    size_t localCount=0,dawCount=0;
    std::array<Event,eventCapacity> pendingLocal{},pendingDaw{};
    size_t pendingLocalCount=0,pendingDawCount=0;
    void* outputContext=nullptr;
    void (*output)(void*,int,uint8_t,uint8_t,uint8_t)=nullptr;
    uint64_t samplePosition=0,timelinePosition=0,timeGeneration=1;
    std::atomic<uint64_t> timecodeSerial{0},timecodeFrame{0},timecodeGeneration{1};
    std::atomic<double> timecodeRate{48000};std::atomic<bool> timecodePlaying{false};
    struct Deferred{uint64_t due=0;InstanceID source=0,target=0;uint64_t sourceGeneration=0,targetGeneration=0;int sourcePort=0,port=0;bool start=false;};
    std::array<Deferred,eventCapacity> deferred{};size_t deferredCount=0;
    std::atomic<uint32_t> deferredOverflow{0};std::atomic<uint64_t> kernelFaults{0};
    void publishTimecode(bool running){
        timecodeSerial.fetch_add(1,std::memory_order_acq_rel);
        timecodeFrame.store(timelinePosition,std::memory_order_relaxed);timecodeGeneration.store(timeGeneration,std::memory_order_relaxed);
        timecodeRate.store(rate,std::memory_order_relaxed);timecodePlaying.store(running,std::memory_order_relaxed);
        timecodeSerial.fetch_add(1,std::memory_order_release);
    }
    void releaseHost(Runtime& node){
        node.hostOutput.controllers.release([&](uint8_t s,uint8_t a,uint8_t b){if(output)output(outputContext,0,s,a,b);});
        if(output)for(int k=0;k<2048;++k)if(node.hostOutput.after[k].velocity)
            output(outputContext,0,uint8_t(0x80|k%16),uint8_t(k/16),0);
        node.hostOutput.clear();
    }
    static bool sameConfig(const GraphNode& a,const GraphNode& b){
        if(a.parameters!=b.parameters||a.texts!=b.texts||a.sample!=b.sample||a.model.pitches!=b.model.pitches||
           a.clip.lengthBeats!=b.clip.lengthBeats||a.clip.events.size()!=b.clip.events.size())return false;
        for(size_t i=0;i<a.clip.events.size();++i){const auto& x=a.clip.events[i];const auto& y=b.clip.events[i];
            if(x.beat!=y.beat||x.status!=y.status||x.data1!=y.data1||x.data2!=y.data2)return false;}
        return true;
    }
    void deliver(Runtime& target,int port,Trigger event){
        if(port<0||size_t(port)>=target.triggers.size())return;
        auto& count=target.triggerCounts[size_t(port)];auto& buffer=target.triggers[size_t(port)];
        if(count<buffer.size()){
            size_t position=count;
            while(position&&buffer[position-1].frame>event.frame){buffer[position]=buffer[position-1];--position;}
            buffer[position]=event;++count;
        }else kernelFaults.fetch_add(1,std::memory_order_relaxed);
    }
    void dispatch(const std::vector<Entry>& entries,size_t source,int sourcePort,Trigger event,int frames){
        for(const auto& entry:entries)for(size_t p=0;p<entry.triggerInputs.size();++p){
            bool connected=false,delayed=false;for(const auto& link:entry.triggerInputs[p])if(link.node==int(source)&&link.port==sourcePort){connected=true;delayed=link.delayed;break;}
            if(!connected)continue;
            if(delayed){
                if(deferredCount<deferred.size())deferred[deferredCount++]={samplePosition+uint64_t(frames)+uint64_t(event.frame),entries[source].state->saved.id,entry.state->saved.id,entries[source].state->generation,entry.state->generation,sourcePort,int(p),event.start};
                else deferredOverflow.fetch_add(1,std::memory_order_relaxed);
            } else deliver(*entry.state,int(p),event);
        }
    }
    void processKernel(const std::vector<Entry>& entries,size_t index,int frames,int offset,const float* inL,const float* inR){
        const auto& entry=entries[index];auto& node=*entry.state;if(!node.kernel)return;
        for(size_t p=0;p<node.inputs.size();++p){
            const auto& declaration=entry.spec->inputs[p];KernelInput input;input.id=declaration.id;
            const auto link=entry.inputs[p];auto* source=link.connected()?entries[size_t(link.node)].state.get():nullptr;
            if(declaration.signal==Signal::Audio&&source){input.left=source->left(link.port);input.right=source->right(link.port);}
            else if(declaration.signal==Signal::Midi){
                auto* upstream=source?source->ports[size_t(link.port)].notes.get():nullptr;
                auto& held=*node.held[p];auto& events=node.midiInputs[p];size_t count=0;
                auto append=[&](Event e){if(count<events.size())events[count++]=e;else kernelFaults.fetch_add(1,std::memory_order_relaxed);};
                const uint64_t generation=source?source->generation:0;
                const bool changed=node.inputGenerations[p]!=generation;
                if(changed)node.inputControllers[p].release([&](uint8_t s,uint8_t a,uint8_t b){append({0,s,a,b});});
                for(int k=0;k<2048;++k){const Note desired=upstream?upstream->before[k]:Note{};
                    if(!changed&&held[k].velocity==desired.velocity&&held[k].token==desired.token)continue;
                    if(held[k].velocity)append({0,uint8_t(0x80|k%16),uint8_t(k/16),0});
                    if(desired.velocity)append({0,uint8_t(0x90|k%16),uint8_t(k/16),desired.velocity});}
                if(upstream)for(size_t i=0;i<upstream->count;++i){append(upstream->events[i]);node.inputControllers[p].observe(upstream->events[i].status,upstream->events[i].a,upstream->events[i].b);}
                held=upstream?upstream->after:Notes{};node.inputGenerations[p]=generation;
                input.midi=events.data();input.midiCount=count;
            } else if(declaration.signal==Signal::Trigger){input.triggers=node.triggers[p].data();input.triggerCount=node.triggerCounts[p];}
            else if(declaration.signal==Signal::Clock&&source)input.timecode=&source->ports[size_t(link.port)].timecode;
            node.inputs[p]=input;
        }
        for(size_t p=0;p<node.outputs.size();++p){
            auto& storage=node.ports[p];const auto& declaration=entry.spec->outputs[p];KernelOutput out;out.id=declaration.id;
            storage.left.fill(0);storage.right.fill(0);if(storage.notes)storage.notes->begin();
            if(declaration.signal==Signal::Audio){out.left=storage.left.data();out.right=storage.right.data();}
            if(declaration.signal==Signal::Midi){out.midi=storage.midi.data();out.midiCapacity=storage.midi.size();}
            if(declaration.signal==Signal::Trigger){out.triggers=storage.triggers.data();out.triggerCapacity=storage.triggers.size();}
            if(declaration.signal==Signal::Clock)out.timecode=&storage.timecode;
            node.outputs[p]=out;
        }
        KernelOutput host;host.midi=node.hostEvents.data();host.midiCapacity=node.hostEvents.size();
        KernelContext c;c.instanceID=node.saved.id;c.sampleRate=rate;c.frames=frames;
        c.framePosition=timelinePosition;c.timeGeneration=timeGeneration;
        c.playing=processing.load(std::memory_order_relaxed)&&(!transport.load(std::memory_order_relaxed)||playing.load(std::memory_order_relaxed));
        c.bypassed=entry.bypass;c.inputs=node.inputs.data();c.inputCount=node.inputs.size();c.outputs=node.outputs.data();c.outputCount=node.outputs.size();
        c.hostAudioLeft=inL?inL+offset:nullptr;c.hostAudioRight=inR?inR+offset:nullptr;
        if(offset==0){c.localMidi=pendingLocal.data();c.localMidiCount=pendingLocalCount;c.dawMidi=pendingDaw.data();c.dawMidiCount=pendingDawCount;}
        c.hostMidi=&host;
        std::fill(node.triggerCounts.begin(),node.triggerCounts.end(),0);
        node.kernel->process(c);
        auto valid=[&](const Event& e){return e.frame>=0&&e.frame<frames&&e.status>=0x80&&e.status<0xf0&&e.a<128&&e.b<128;};
        for(size_t p=0;p<node.outputs.size();++p){
            auto& out=node.outputs[p];auto& storage=node.ports[p];if(out.overflow)kernelFaults.fetch_add(1,std::memory_order_relaxed);
            if(storage.notes){for(size_t i=0;i<std::min(out.midiCount,storage.midi.size());++i){
                if(valid(storage.midi[i]))storage.notes->emit(storage.midi[i]);else kernelFaults.fetch_add(1,std::memory_order_relaxed);}
                if(out.overflow)storage.notes->overflow=true;}
            if(entry.spec->outputs[p].signal==Signal::Audio)for(int i=0;i<frames;++i){
                if(!std::isfinite(storage.left[i])){storage.left[i]=0;kernelFaults.fetch_add(1,std::memory_order_relaxed);}
                if(!std::isfinite(storage.right[i])){storage.right[i]=0;kernelFaults.fetch_add(1,std::memory_order_relaxed);}}
            for(size_t i=0;i<std::min(out.triggerCount,storage.triggers.size());++i){auto event=storage.triggers[i];
                if(event.frame>=0&&event.frame<frames)dispatch(entries,index,int(p),event,frames);else kernelFaults.fetch_add(1,std::memory_order_relaxed);}
        }
        node.hostOutput.begin();
        for(size_t i=0;i<std::min(host.midiCount,node.hostEvents.size());++i){const auto event=node.hostEvents[i];
            if(!valid(event)){kernelFaults.fetch_add(1,std::memory_order_relaxed);continue;}
            node.hostOutput.emit(event);if(output)output(outputContext,offset+event.frame,event.status,event.a,event.b);}
        if(host.overflow||node.hostOutput.overflow){releaseHost(node);kernelFaults.fetch_add(1,std::memory_order_relaxed);}
    }
    void cancelMidiCaptureLocked(){if(activeCapture.recording){activeCapture.recording->stop();activeCapture={};capturePlans.publish(activeCapture);}}
public:
    KernelTimecode timecodeSnapshot()const{KernelTimecode result;for(;;){uint64_t serial=timecodeSerial.load(std::memory_order_acquire);if(serial&1)continue;
        result={timecodeFrame.load(std::memory_order_relaxed),timecodeGeneration.load(std::memory_order_relaxed),timecodeRate.load(std::memory_order_relaxed),timecodePlaying.load(std::memory_order_relaxed)};
        if(serial==timecodeSerial.load(std::memory_order_acquire))return result;}}
    bool enabled()const{return active.load(std::memory_order_acquire);}
    uint32_t droppedFeedbackEvents()const{return deferredOverflow.load(std::memory_order_relaxed);}
    uint64_t kernelFaultCount()const{return kernelFaults.load(std::memory_order_relaxed);}
    bool publish(const GraphSpec& source,bool allowUnknown=false){
        NodeCatalogReadScope catalogScope;
        try {
        GraphSpec graph=source;if(!normalizeGraph(graph))return false;
        CompiledGraph compiled;if(!compileGraph(graph,compiled,nullptr,allowUnknown))return false;
        std::lock_guard<std::mutex> lock(edits);Plan plan;plan.catalog=compiled.catalog;
        const bool projectChanged=controlProject!=graph.projectId;
        std::string nextProject=graph.projectId;
        plan.projectEpoch=controlProjectEpoch+(projectChanged?1:0);
        std::map<InstanceID,std::shared_ptr<Runtime>> replacement;std::vector<int> entryOf(graph.nodes.size(),-1);
        for(size_t i:compiled.order){const auto& node=graph.nodes[i];const auto& info=compiled.nodes[i];if(!info.spec)continue;
            auto old=controlNodes.find(node.id);const Runtime* previous=old!=controlNodes.end()&&!projectChanged?old->second.get():nullptr;
            const bool reuse=previous&&previous->spec==info.spec&&sameConfig(previous->saved,node)&&previous->saved.nodeVersion==node.nodeVersion&&previous->saved.stateVersion==node.stateVersion;
            auto runtime=reuse?old->second:std::make_shared<Runtime>(node,info.spec,rate,nextGeneration++,plan.catalog,previous);
            if(!reuse&&previous&&previous->stopRequested.load(std::memory_order_acquire))runtime->stopRequested.store(true,std::memory_order_release);
            Entry entry;entry.state=runtime;entry.spec=info.spec;entry.inputs=info.inputs;entry.triggerInputs=info.triggerInputs;
            entry.outputSink=nodeHas(info.spec->capabilities,CapOutputSink);entry.bypass=info.bypass;
            for(size_t p=0;p<info.spec->inputs.size();++p)if(info.spec->inputs[p].role==RoleMain&&info.spec->inputs[p].signal==Signal::Midi)entry.mainNote=info.inputs[p];
            entryOf[i]=int(plan.entries.size());plan.byID.emplace(node.id,plan.entries.size());plan.entries.push_back(std::move(entry));replacement[node.id]=runtime;
        }
        std::map<InstanceID,CaptureRoute> nextRoutes;
        for(auto& entry:plan.entries){auto remap=[&](PortLink& link){if(link.node>=0){link.node=entryOf[size_t(link.node)];if(link.node<0)link.port=-1;}};
            for(auto& link:entry.inputs)remap(link);for(auto& inputs:entry.triggerInputs){for(auto& link:inputs)remap(link);inputs.erase(std::remove_if(inputs.begin(),inputs.end(),[](const PortLink& l){return !l.connected();}),inputs.end());}
            remap(entry.mainNote);
            if(nodeHas(entry.spec->capabilities,CapMidiCapture)&&entry.mainNote.connected())nextRoutes.emplace(entry.state->saved.id,CaptureRoute{entry.state->generation,plan.entries[size_t(entry.mainNote.node)].state->generation,entry.mainNote.port});}
        bool cancelCapture=false;
        if(activeCapture.recording){auto next=nextRoutes.find(activeCapture.node),old=captureRoutes.find(activeCapture.node);
            cancelCapture=projectChanged||next==nextRoutes.end()||old==captureRoutes.end()||!(next->second==old->second);}
        auto nextRetired=controlRetired;
        for(auto it=nextRetired.begin();it!=nextRetired.end();)if(it->second->retirementHandled.load(std::memory_order_acquire))it=nextRetired.erase(it);else ++it;
        for(auto& old:controlNodes){auto next=replacement.find(old.first);if(next==replacement.end()||next->second!=old.second)nextRetired[old.second->generation]=old.second;}
        for(const auto& old:nextRetired)plan.retired.push_back(old.second);
        // Only a complete, prepared plan crosses the publication boundary.
        // Throwing third-party constructors/configuration leave the live graph
        // and project identity intact. Reclamation stays on this thread.
        plans.publish(plan);
        controlNodes=std::move(replacement);controlRetired=std::move(nextRetired);
        controlProject.swap(nextProject);controlProjectEpoch=plan.projectEpoch;
        if(cancelCapture)cancelMidiCaptureLocked();captureRoutes=std::move(nextRoutes);
        active.store(true,std::memory_order_release);return true;
        }catch(...){return false;}
    }
    uint64_t startMidiCapture(InstanceID node,int tempoParameter){std::lock_guard<std::mutex> lock(edits);
        if(activeCapture.recording||!captureRoutes.count(node))return 0;const auto& config=controlNodes.at(node)->saved;
        if(!nodeAcceptsParam(config.type,tempoParameter))return 0;double tempo=graphParamValue(config,tempoParameter);
        if(!std::isfinite(tempo)||tempo<=0)return 0;activeCapture={node,std::make_shared<MidiInputCapture>(rate,tempo)};capturePlans.publish(activeCapture);return ++captureToken;}
    std::shared_ptr<MidiInputCapture> finishMidiCapture(uint64_t token){std::lock_guard<std::mutex> lock(edits);if(!token||token!=captureToken)return {};
        auto recording=activeCapture.recording;cancelMidiCaptureLocked();return recording;}
    void disable(){std::lock_guard<std::mutex> lock(edits);cancelMidiCaptureLocked();captureRoutes.clear();active.store(false,std::memory_order_release);}
    void prepare(double sr){rate=sr>0?sr:48000;for(auto& pair:controlNodes)if(pair.second->kernel)pair.second->kernel->prepare(rate,quantum);reset();}
    void setHostTempo(double){}
    void setHostTransport(bool on,double){playing.store(on,std::memory_order_release);transport.store(true,std::memory_order_release);}
    void setHostTimecode(bool on,uint64_t frame){playing.store(on,std::memory_order_release);transport.store(true,std::memory_order_release);hostFrameRequested.store(frame,std::memory_order_release);}
    void clearHostTransport(){transport.store(false,std::memory_order_release);}
    void setMidiOutput(void* context,void(*fn)(void*,int,uint8_t,uint8_t,uint8_t)){outputContext=context;output=fn;}
    void midi(bool fromDaw,uint8_t s,uint8_t a,uint8_t b){
        if(s<0x80||s>=0xf0||a>127||b>127)return;
        uint64_t generation=stopGeneration.load(std::memory_order_acquire);if(!processing.load(std::memory_order_acquire))return;
        std::unique_lock<std::mutex> lock(midiLock,std::try_to_lock);if(!lock.owns_lock()){inputOverflow.store(true,std::memory_order_release);return;}
        if(!processing.load(std::memory_order_acquire)||generation!=stopGeneration.load(std::memory_order_acquire))return;
        auto& count=fromDaw?dawCount:localCount;auto& queue=fromDaw?daw:local;
        if(count<queue.size())queue[count++]={{0,s,a,b},generation};else inputOverflow.store(true,std::memory_order_release);
    }
    void reset(bool preserveQueued=false,KernelResetReason reason=KernelResetReason::Stop){++timeGeneration;
        for(const auto& entry:plans.current().entries){releaseHost(*entry.state);entry.state->reset(reason);}
        deferredCount=0;pendingLocalCount=pendingDawCount=0;inputOverflow.store(false,std::memory_order_relaxed);
        if(!preserveQueued){std::unique_lock<std::mutex> lock(midiLock,std::try_to_lock);if(lock.owns_lock())localCount=dawCount=0;else stopGeneration.fetch_add(1,std::memory_order_acq_rel);}}
    void panic(){panicRequested.store(true,std::memory_order_release);}
    void seekTimecode(uint64_t frame){seekRequested.store(frame,std::memory_order_release);}
    void requestNodeStop(InstanceID id){std::lock_guard<std::mutex> lock(edits);auto found=controlNodes.find(id);if(found!=controlNodes.end())found->second->stopRequested.store(true,std::memory_order_release);}
    void setProcessingEnabled(bool value){if(!value){processing.store(false,std::memory_order_release);stopGeneration.fetch_add(1,std::memory_order_acq_rel);stopResetRequested.store(true,std::memory_order_release);}processing.store(value,std::memory_order_release);}
    void render(const float* inL,const float* inR,float* outL,float* outR,int frames){
        if(frames<=0||!outL||!outR)return;capturePlans.consume();const auto& capture=capturePlans.current();
        bool changed=plans.consume();if(changed){
            for(auto& retired:plans.current().retired)if(!retired->retirementHandled.exchange(true,std::memory_order_acq_rel)){releaseHost(*retired);retired->reset(KernelResetReason::Removed);}
            if(renderProjectEpoch!=plans.current().projectEpoch){timelinePosition=0;if(renderProjectEpoch)reset(false,KernelResetReason::Seek);renderProjectEpoch=plans.current().projectEpoch;}}
        if(panicRequested.exchange(false,std::memory_order_acq_rel))reset(false,KernelResetReason::Panic);
        const bool stopping=stopResetRequested.exchange(false,std::memory_order_acq_rel);
        if(stopping||(changed&&!processing.load(std::memory_order_acquire)))reset(true);
        uint64_t seek=seekRequested.exchange(UINT64_MAX,std::memory_order_acq_rel);if(seek!=UINT64_MAX){timelinePosition=seek;reset(false,KernelResetReason::Seek);}
        uint64_t host=hostFrameRequested.exchange(UINT64_MAX,std::memory_order_acq_rel);if(host!=UINT64_MAX&&host!=timelinePosition){timelinePosition=host;reset(true,KernelResetReason::Seek);}
        for(const auto& entry:plans.current().entries)if(entry.state->stopRequested.exchange(false,std::memory_order_acq_rel)){
            releaseHost(*entry.state);entry.state->reset(KernelResetReason::Stop);
            size_t kept=0;for(size_t i=0;i<deferredCount;++i)
                if(deferred[i].source!=entry.state->saved.id&&deferred[i].target!=entry.state->saved.id)deferred[kept++]=deferred[i];
            deferredCount=kept;
        }
        if(!processing.load(std::memory_order_acquire)){std::fill_n(outL,frames,0.f);std::fill_n(outR,frames,0.f);publishTimecode(false);return;}
        pendingLocalCount=pendingDawCount=0;
        {std::unique_lock<std::mutex> lock(midiLock,std::try_to_lock);if(lock.owns_lock()){uint64_t generation=stopGeneration.load(std::memory_order_acquire);
            for(size_t i=0;i<localCount;++i)if(local[i].generation==generation)pendingLocal[pendingLocalCount++]=local[i].event;
            for(size_t i=0;i<dawCount;++i)if(daw[i].generation==generation)pendingDaw[pendingDawCount++]=daw[i].event;localCount=dawCount=0;}}
        if(inputOverflow.exchange(false,std::memory_order_acq_rel))reset(true);
        const auto& entries=plans.current().entries;
        for(int offset=0;offset<frames;offset+=quantum){int count=std::min(quantum,frames-offset);
            size_t keep=0;
            for(size_t i=0;i<deferredCount;++i){
                const auto event=deferred[i];const auto& lookup=plans.current().byID;
                auto source=lookup.find(event.source),target=lookup.find(event.target);
                if(source==lookup.end()||target==lookup.end())continue;
                const auto& from=entries[source->second];const auto& to=entries[target->second];
                if(from.state->generation!=event.sourceGeneration||to.state->generation!=event.targetGeneration||
                   event.port<0||size_t(event.port)>=to.triggerInputs.size())continue;
                bool connected=false;
                for(const auto& link:to.triggerInputs[size_t(event.port)])
                    if(link.delayed&&link.node==int(source->second)&&link.port==event.sourcePort){connected=true;break;}
                if(!connected)continue;
                if(event.due<samplePosition+uint64_t(count))
                    deliver(*to.state,event.port,{int(event.due>samplePosition?event.due-samplePosition:0),event.start});
                else deferred[keep++]=event;
            }
            deferredCount=keep;
            for(size_t i=0;i<entries.size();++i){const auto& entry=entries[i];
                if(capture.recording&&capture.node==entry.state->saved.id&&entry.mainNote.connected()){
                    auto* notes=entries[size_t(entry.mainNote.node)].state->ports[size_t(entry.mainNote.port)].notes.get();
                    if(notes){if(notes->overflow)capture.recording->overflow();else capture.recording->append(notes->events.data(),notes->count,count);}}
                processKernel(entries,i,count,offset,inL,inR);}
            for(int frame=0;frame<count;++frame){float left=0,right=0;for(const auto& entry:entries)if(entry.outputSink)
                for(size_t p=0;p<entry.spec->outputs.size();++p)if(entry.spec->outputs[p].signal==Signal::Audio){left+=entry.state->left(int(p))[frame];right+=entry.state->right(int(p))[frame];}
                outL[offset+frame]=left;outR[offset+frame]=right;}
            samplePosition+=count;if(!transport.load(std::memory_order_relaxed)||playing.load(std::memory_order_relaxed))timelinePosition+=count;
        }
        publishTimecode(!transport.load(std::memory_order_relaxed)||playing.load(std::memory_order_relaxed));
    }
};
}
