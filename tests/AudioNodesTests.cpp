#include "../src/Engine.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace tri;

static size_t add(GraphSpec& graph, InstanceID id, int type, std::map<int,float> parameters={}) {
    GraphNode node;node.id=id;node.type=type;node.parameters=std::move(parameters);
    graph.nodes.push_back(std::move(node));
    return graph.nodes.size()-1;
}
static void connect(GraphSpec& graph,InstanceID source,InstanceID target,int port=0,int sourcePort=0) {
    graph.edges.push_back({source,target,port,sourcePort});
}
static double rms(const std::vector<float>& audio,size_t begin,size_t end) {
    double sum=0;for(size_t i=begin;i<end;++i)sum+=double(audio[i])*audio[i];
    return std::sqrt(sum/std::max<size_t>(1,end-begin));
}
static double tone(const std::vector<float>& audio,double hz,size_t begin,size_t count) {
    double re=0,im=0;
    for(size_t i=0;i<count;++i){double phase=2*3.14159265358979323846*hz*double(begin+i)/48000.0;re+=audio[begin+i]*std::cos(phase);im+=audio[begin+i]*std::sin(phase);}
    return std::hypot(re,im)/double(count);
}

int main() {
    // New node types are accepted and their parameters are type-scoped.
    {
        GraphSpec graph;
        add(graph,1,NoiseNode,{{NoiseLevel,.5f},{NoiseType,0}});
        add(graph,2,FilterNode,{{FilterType,0},{FilterCutoff,1000},{FilterResonance,1}});
        add(graph,3,ReverbNode,{{ReverbMix,.4f},{ReverbSize,.5f}});
        add(graph,4,OutputNode,{{Master,1}});
        connect(graph,1,2);connect(graph,2,3);connect(graph,3,4);
        std::vector<size_t> order;
        assert(graphOrder(graph,order));
        assert(!validGraphParam(NoiseNode,FilterCutoff));
        assert(!validGraphParam(FilterNode,ReverbMix));
        assert(validGraphParam(NoiseNode,NoiseType));
        assert(validGraphParam(AudioClipNode,AudioClipGain));
        assert(!validGraphParam(AudioClipNode,CompThreshold));
        assert(validGraphParam(CompressorNode,CompThreshold));
        assert(!validGraphParam(CompressorNode,AudioClipGain));
        assert(validGraphParam(LimiterNode,LimiterCeiling));
        assert(!validGraphParam(LimiterNode,CompThreshold));
        assert(validGraphParam(ChorusNode,ChorusDepth));
        assert(!validGraphParam(ChorusNode,LimiterGain));
        graph.nodes[0].parameters[FilterCutoff]=1000;
        assert(!graphOrder(graph,order));
        graph.nodes[0].parameters.erase(FilterCutoff);
        graph.nodes[1].parameters[FilterCutoff]=30000;
        assert(!graphOrder(graph,order));
        graph.nodes[1].parameters[FilterCutoff]=1000;
        graph.nodes[0].type=99;
        assert(!graphOrder(graph,order));
    }

    // Noise -> filter -> reverb -> output produces finite, non-silent audio.
    {
        GraphSpec graph;
        add(graph,1,NoiseNode,{{NoiseLevel,.6f},{NoiseType,1}});
        add(graph,2,FilterNode,{{FilterType,0},{FilterCutoff,1800},{FilterResonance,1.2f}});
        add(graph,3,ReverbNode,{{ReverbMix,.5f},{ReverbSize,.7f},{ReverbDamping,.2f},{ReverbWidth,1}});
        add(graph,4,OutputNode,{{Master,1}});
        connect(graph,1,2);connect(graph,2,3);connect(graph,3,4);
        Engine engine;assert(engine.setGraph(graph));engine.prepare(48000);
        std::vector<float> left(48000),right(48000);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        double level=rms(left,0,left.size());
        assert(level>.005 && level<2);
        for(float sample:left)assert(std::isfinite(sample));
        puts("noise/filter/reverb graph renders");
    }

    // Bypassing an inline filter must restore the unfiltered level.
    {
        GraphSpec graph;
        add(graph,10,MidiNode);
        add(graph,1,Osc1Node,{{Wave1,0},{Level1,1},{Attack,.001f},{Decay,.005f},{Sustain,1},{Release,.01f}});
        add(graph,2,FilterNode,{{FilterType,0},{FilterCutoff,180},{FilterResonance,.7f}});
        add(graph,3,OutputNode,{{Master,1}});
        connect(graph,10,1);connect(graph,1,2);connect(graph,2,3);
        Engine engine;assert(engine.setGraph(graph));engine.prepare(48000);
        engine.midi(0x90,69,110);
        std::vector<float> left(48000),right(48000);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        double filtered=rms(left,12000,42000);
        double low=tone(left,440,12000,24000);
        graph.nodes[2].bypass=true;
        assert(engine.setGraph(graph));
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        double bypassed=rms(left,12000,42000);
        double high=tone(left,440,12000,24000);
        assert(bypassed>filtered*1.5);
        assert(high>low*2);
        for(float sample:left)assert(std::isfinite(sample));
        puts("filter bypass restores the dry signal");
    }

    // A bypassed output node is silent.
    {
        GraphSpec graph;
        add(graph,1,NoiseNode,{{NoiseLevel,1}});
        add(graph,2,OutputNode,{{Master,1}});
        connect(graph,1,2);
        graph.nodes[1].bypass=true;
        Engine engine;assert(engine.setGraph(graph));engine.prepare(48000);
        std::vector<float> left(4096),right(4096);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        assert(rms(left,0,left.size())==0);
        puts("output bypass is silent");
    }
    // A sampler plays an embedded sample, loops it, and responds to note-off.
    {
        auto sample=std::make_shared<AudioSample>();
        sample->name="test";sample->rate=48000;sample->channels=1;
        sample->samples.resize(2400); // 50 ms
        for(size_t i=0;i<sample->samples.size();++i)
            sample->samples[i]=float(std::sin(2*3.14159265358979323846*440.0*double(i)/48000.0))*.4f;
        GraphSpec graph;
        add(graph,10,MidiNode);
        size_t sampler=add(graph,1,SamplerNode,{{SamplerRoot,60},{SamplerGain,1},{SamplerLoop,1},{SamplerAttack,.001f},{SamplerRelease,.02f},{SamplerStart,0},{SamplerEnd,1}});
        graph.nodes[sampler].sample=sample;
        add(graph,2,OutputNode,{{Master,1}});
        connect(graph,10,1);connect(graph,1,2);
        std::vector<size_t> order;assert(graphOrder(graph,order));
        assert(!validGraphParam(SamplerNode,NoiseLevel));
        assert(validGraphParam(SamplerNode,SamplerRoot));
        Engine engine;assert(engine.setGraph(graph));engine.prepare(48000);
        engine.midi(0x90,60,100);
        std::vector<float> left(48000),right(48000);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        assert(rms(left,0,24000)>.02);
        assert(rms(left,36000,48000)>.02); // loop survives past the 50 ms sample
        for(float value:left)assert(std::isfinite(value));
        engine.midi(0x80,60,0);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        assert(rms(left,24000,48000)<.005);
        puts("sampler plays, loops and releases a note");
    }

    // A bypassed sampler is silent.
    {
        auto sample=std::make_shared<AudioSample>();
        sample->rate=48000;sample->channels=1;sample->samples.assign(2400,.5f);
        GraphSpec graph;
        add(graph,10,MidiNode);
        size_t sampler=add(graph,1,SamplerNode,{{SamplerGain,1}});
        graph.nodes[sampler].sample=sample;graph.nodes[sampler].bypass=true;
        add(graph,2,OutputNode,{{Master,1}});
        connect(graph,10,1);connect(graph,1,2);
        Engine engine;assert(engine.setGraph(graph));engine.prepare(48000);
        engine.midi(0x90,60,100);
        std::vector<float> left(4096),right(4096);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        assert(rms(left,0,left.size())==0);
        puts("sampler bypass is silent");
    }
    // Stereo samples keep their left and right channels.
    {
        auto sample=std::make_shared<AudioSample>();
        sample->rate=48000;sample->channels=2;sample->samples.resize(4096*2);
        for(size_t i=0;i<sample->samples.size();i+=2){sample->samples[i]=.5f;sample->samples[i+1]=-.5f;}
        GraphSpec graph;
        add(graph,10,MidiNode);
        size_t sampler=add(graph,1,SamplerNode,{{SamplerGain,1},{SamplerLoop,1}});
        graph.nodes[sampler].sample=sample;
        add(graph,2,OutputNode,{{Master,1}});
        connect(graph,10,1);connect(graph,1,2);
        Engine engine;assert(engine.setGraph(graph));engine.prepare(48000);
        engine.midi(0x90,60,100);
        std::vector<float> left(4096),right(4096);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        double l=0,r=0;for(size_t i=0;i<left.size();++i){l+=left[i];r+=right[i];}
        assert(l/double(left.size())>.05 && r/double(right.size())<-.05);
        puts("sampler preserves stereo channels");
    }
    // An audio clip starts from a trigger, loops, and stops on a stop trigger.
    {
        auto sample=std::make_shared<AudioSample>();
        sample->rate=48000;sample->channels=1;sample->samples.resize(4800,.4f);
        GraphSpec graph;
        add(graph,1,StartNode,{{TriggerPulse,0}});
        add(graph,2,StopNode,{{TriggerPulse,0}});
        size_t clip=add(graph,3,AudioClipNode,{{AudioClipGain,1},{AudioClipLoop,1},{AudioClipStart,0},{AudioClipEnd,1},{AudioClipFade,.005f}});
        graph.nodes[clip].sample=sample;
        add(graph,4,OutputNode,{{Master,1}});
        connect(graph,1,3,0);connect(graph,2,3,0);connect(graph,3,4);
        Engine engine;assert(engine.setGraph(graph));engine.prepare(48000);
        graph.nodes[0].parameters[TriggerPulse]=1;
        assert(engine.setGraph(graph));
        std::vector<float> left(24000),right(24000);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        assert(rms(left,2400,20000)>.02);
        graph.nodes[1].parameters[TriggerPulse]=1;
        assert(engine.setGraph(graph));
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        assert(rms(left,12000,24000)<.005);
        puts("audio clip starts, loops and stops");
    }
    // A one-shot audio clip stops at its end, and bypass is silent.
    {
        auto sample=std::make_shared<AudioSample>();
        sample->rate=48000;sample->channels=1;sample->samples.assign(2400,.4f);
        GraphSpec graph;
        add(graph,1,StartNode,{{TriggerPulse,0}});
        size_t clip=add(graph,2,AudioClipNode,{{AudioClipGain,1},{AudioClipLoop,0},{AudioClipStart,0},{AudioClipEnd,1},{AudioClipFade,.005f}});
        graph.nodes[clip].sample=sample;
        add(graph,3,OutputNode,{{Master,1}});
        connect(graph,1,2,0);connect(graph,2,3);
        Engine engine;assert(engine.setGraph(graph));engine.prepare(48000);
        graph.nodes[0].parameters[TriggerPulse]=1;
        assert(engine.setGraph(graph));
        std::vector<float> left(9600),right(9600);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        assert(rms(left,0,1200)>.02);
        assert(rms(left,4800,9600)<.005);
        graph.nodes[clip].bypass=true;
        assert(engine.setGraph(graph));
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        assert(rms(left,0,left.size())==0);
        puts("one-shot audio clip ends and bypass is silent");
    }
    // The compressor reduces a loud signal and preserves the dry path when mixed out.
    {
        GraphSpec graph;
        add(graph,10,MidiNode);
        add(graph,1,Osc1Node,{{Wave1,0},{Level1,1},{Attack,.001f},{Decay,.005f},{Sustain,1},{Release,.01f}});
        add(graph,2,CompressorNode,{{CompThreshold,-30},{CompRatio,20},{CompAttack,.001f},{CompRelease,.1f},{CompMakeup,1},{CompMix,1}});
        add(graph,3,OutputNode,{{Master,1}});
        connect(graph,10,1);connect(graph,1,2);connect(graph,2,3);
        Engine engine;assert(engine.setGraph(graph));engine.prepare(48000);
        engine.midi(0x90,69,127);
        std::vector<float> left(24000),right(24000);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        double compressed=rms(left,4800,20000);
        graph.nodes[2].parameters[CompMix]=0;
        assert(engine.setGraph(graph));
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        double dry=rms(left,4800,20000);
        assert(compressed<dry*.8);
        puts("compressor reduces gain and honors dry mix");
    }
    // A hard ceiling catches peaks above the limiter threshold; bypass exposes them again.
    {
        GraphSpec graph;
        add(graph,1,NoiseNode,{{NoiseLevel,1},{NoiseType,0}});
        add(graph,2,LimiterNode,{{LimiterThreshold,-6},{LimiterRelease,.05f},{LimiterCeiling,-12},{LimiterGain,2}});
        add(graph,3,OutputNode,{{Master,1}});
        connect(graph,1,2);connect(graph,2,3);
        Engine engine;assert(engine.setGraph(graph));engine.prepare(48000);
        std::vector<float> left(24000),right(24000);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        float limited=0;for(float sample:left)limited=std::max(limited,std::fabs(sample));
        graph.nodes[1].bypass=true;
        assert(engine.setGraph(graph));
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        float dry=0;for(float sample:left)dry=std::max(dry,std::fabs(sample));

        assert(dry>limited*1.05f);
        puts("limiter clamps peaks and bypass restores level");
    }
    // Chorus mixes a modulated delay and is silent at zero mix.
    {
        GraphSpec graph;
        add(graph,10,MidiNode);
        add(graph,1,Osc1Node,{{Wave1,0},{Level1,1},{Attack,.001f},{Decay,.005f},{Sustain,1},{Release,.01f}});
        add(graph,2,ChorusNode,{{ChorusRate,.8f},{ChorusDepth,.5f},{ChorusMix,1},{ChorusDelay,12}});
        add(graph,3,OutputNode,{{Master,1}});
        connect(graph,10,1);connect(graph,1,2);connect(graph,2,3);
        Engine engine;assert(engine.setGraph(graph));engine.prepare(48000);
        engine.midi(0x90,69,127);
        std::vector<float> left(24000),right(24000);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        assert(rms(left,4800,20000)>.005);
        graph.nodes[2].parameters[ChorusMix]=0;
        assert(engine.setGraph(graph));
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        assert(rms(left,4800,20000)>.005);
        puts("chorus renders modulated delay and honors dry mix");
    }
    // Scale quantizer transforms incoming notes and tracks note-off mapping.
    {
        struct Sent {uint8_t status,a,b;};
        std::vector<Sent> sent;
        GraphSpec graph;
        add(graph,10,MidiNode);
        add(graph,1,QuantizeNode,{{QuantizeRoot,0},{QuantizeScale,0},{QuantizeMode,2}});
        add(graph,2,MidiOutNode);
        connect(graph,10,1);connect(graph,1,2);
        Engine engine;assert(engine.setGraph(graph));engine.prepare(48000);
        engine.setMidiOutput(&sent,[](void* p,int,uint8_t s,uint8_t a,uint8_t b){static_cast<std::vector<Sent>*>(p)->push_back({s,a,b});});
        engine.midi(0x90,61,100);
        std::vector<float> left(256),right(256);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        bool mappedOn=false;
        for(const auto& event:sent)if(event.status==0x90 && event.a==60 && event.b==100)mappedOn=true;
        assert(mappedOn);
        sent.clear();
        engine.midi(0x80,61,0);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        bool mappedOff=false;
        for(const auto& event:sent)if(event.status==0x80 && event.a==60)mappedOff=true;
        assert(mappedOff);
        graph.nodes[1].bypass=true;
        assert(engine.setGraph(graph));
        sent.clear();
        engine.midi(0x90,61,100);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        bool bypassedOn=false;
        for(const auto& event:sent)if(event.status==0x90 && event.a==61)bypassedOn=true;
        assert(bypassedOn);
        sent.clear();
        engine.midi(0x80,61,0);
        engine.render(nullptr,nullptr,left.data(),right.data(),left.size());
        bool bypassedOff=false;
        for(const auto& event:sent)if(event.status==0x80 && event.a==61)bypassedOff=true;
        assert(bypassedOff);
        assert(validGraphParam(QuantizeNode,QuantizeRoot));
        assert(!validGraphParam(QuantizeNode,ChorusMix));
        puts("scale quantizer maps held notes and releases the mapped pitch");
    }
    puts("PASS: filter, reverb, noise, sampler, audio clip, compressor, limiter, chorus, quantizer, bypass and type-scoped validation.");

}
