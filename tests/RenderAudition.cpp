// Builds a stereo performance from four instances of the production engine.
// Usage: render-audition /path/to/output.wav
#include "../src/Engine.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace tri;
constexpr int fs = 48000;
struct Event {
    int frame, track, status, data1, data2;
};
struct Instrument {
    Engine e;
    float l, r;
};
void sound(Engine& e,int a,int b,int c,float l1,float l2,float l3) {
    e.set(Wave1,a); e.set(Wave2,b); e.set(Wave3,c);
    e.set(Level1,l1); e.set(Level2,l2); e.set(Level3,l3);
    e.set(Tune1,0); e.set(Tune2,12); e.set(Tune3,24); e.set(Master,.78);
}
void patch(Instrument* tracks,bool reggae) {
    for (int i=0;i<2;++i) {
        auto& e=tracks[i].e;
        if (reggae) sound(e,1,3,0,.40,.34,.17);
        else sound(e,3,0,0,.76,.34,.11);
        e.set(ArpOn,1); e.set(Pattern,reggae?2:1); e.set(Tempo,reggae?84:132);
        e.set(Gate,reggae?.33:.61); e.set(Attack,.002); e.set(Decay,reggae?.095:.21);
        e.set(Sustain,reggae?.16:.34); e.set(Release,reggae?.072:.14);
        e.set(DelayTime,reggae?268:170); e.set(DelayMix,i==0?.14:.07);
        e.set(DelayFeedback,reggae?.35:.20); e.set(EQHigh,reggae?-1.:2.);
        e.set(EQLow,-2.);
    }
    auto& bass=tracks[2].e;
    sound(bass,3,0,2,.68,.55,reggae?.05:.10);
    bass.set(Tune2,0); bass.set(Tune3,12); bass.set(Attack,.008);
    bass.set(Decay,reggae?.32:.22); bass.set(Sustain,reggae?.57:.41);
    bass.set(Release,.068); bass.set(DelayMix,0); bass.set(EQLow,2.);
    auto& wood=tracks[3].e;
    sound(wood,3,1,0,.43,.09,.36); wood.set(Tune2,19); wood.set(Tune3,22);
    wood.set(Attack,.001); wood.set(Decay,.029); wood.set(Sustain,0);
    wood.set(Release,.024); wood.set(DelayMix,.11); wood.set(DelayTime,112);
}
void little(std::FILE* f,uint32_t n,int bytes) {
    for(int i=0;i<bytes;++i) std::fputc((n>>(8*i))&255,f);
}
int main(int argc,char** argv) {
    if(argc!=2) return 1;
    Instrument t[4] = {{ {},.84f,.41f },{ {},.41f,.84f },{ {},.79f,.79f },{ {},.22f,.27f }};
    patch(t,false);
    for(auto& track:t) track.e.prepare(fs);
    std::vector<Event> events;
    const int seam=std::lround(16.*60/132*fs);
    const int end=seam+std::lround(8.*60/84*fs);
    const int frames=end+int(.72*fs);
    auto note=[&](int track,int at,int stop,int pitch,int vel) {
        events.push_back({at,track,0x90,pitch,vel});
        events.push_back({stop,track,0x80,pitch,0});
    };
    // Dm9, G13, Cmaj9, A7(b9), then Dm9 and Gm7/A7.
    const int chords[][6]={{53,57,60,64,65,69},{53,59,62,64,67,71},{52,55,59,62,64,67},{55,58,61,64,67,70},
                           {53,57,60,64,65,69},{53,55,58,62,65,67},{55,58,61,64,67,70}};
    for(int bar=0;bar<6;++bar) {
        const bool reg=bar>=4;
        const double beat=60./(reg?84:132);
        auto at=[&](double q){ return (reg?seam:0)+int(std::lround(((reg?bar-4:bar)*4.+q)*beat*fs)); };
        for(int half=0;half<(bar==5?2:1);++half) {
            const double start=half*2, stop=bar==5 ? start+2 : 4;
            for(int n=0;n<4;++n) {
                note(0,at(start),at(stop)-1,chords[bar+half][n],100);
                note(1,at(start),at(stop)-1,chords[bar+half][n+1],84);
            }
        }
        const int roots[]={38,31,36,33,38,31};
        const int root=roots[bar];
        if(!reg) {
            note(2,at(0),at(.94),root,114);
            note(2,at(1.7),at(1.95),root+12,74);
            note(2,at(2),at(2.88),root+7,106);
            note(2,at(3.5),at(3.94),root+10,86);
            for(double q:{.5,1.25,2.5,3.25}) note(3,at(q),at(q+.09),q<2?79:74,q==1.25?69:48);
        } else {
            note(2,at(0),at(.9),root,120);
            note(2,at(1.5),at(1.78),root+7,78);
            note(2,at(2),at(2.85),bar==4?root+12:33,109);
            note(2,at(3.25),at(3.48),bar==4?root+7:40,87);
            note(2,at(3.55),at(3.85),bar==4?root+3:45,80);
            note(3,at(2),at(2.12),73,99);
            note(3,at(3.75),at(3.86),80,45);
        }
    }
    events.push_back({seam,-1,0,0,0});
    // Preserve insertion order on ties; apply the new patch before the first reggae notes.
    std::stable_sort(events.begin(),events.end(),[](const Event&a,const Event&b){
        if(a.frame!=b.frame) return a.frame<b.frame;
        return a.track<0 && b.track>=0;
    });
    std::vector<float> stereo(frames*2);
    std::array<float,256> L{},R{};
    size_t ev=0; int pos=0;
    while(pos<frames) {
        while(ev<events.size() && events[ev].frame==pos) {
            auto m=events[ev++];
            if(m.track<0) patch(t,true); else t[m.track].e.midi(m.status,m.data1,m.data2);
        }
        const int next=ev<events.size()?events[ev].frame:frames;
        const int n=std::min(256,next-pos);
        for(auto& track:t) {
            track.e.render(nullptr,nullptr,L.data(),R.data(),n);
            for(int i=0;i<n;++i) {
                stereo[2*(pos+i)]+=track.l*L[i];
                stereo[2*(pos+i)+1]+=track.r*R[i];
            }
        }
        pos+=n;
    }
    float peak=0;
    for(float s:stereo) peak=std::max(peak,std::abs(s));
    if(peak<.03f) return 2;
    auto f=std::fopen(argv[1],"wb"); if(!f) return 3;
    auto bytes=[&](const char* str){std::fwrite(str,1,4,f);};
    const int dataSize=static_cast<int>(stereo.size()*sizeof(int16_t));
    bytes("RIFF"); little(f,36+dataSize,4); bytes("WAVE"); bytes("fmt "); little(f,16,4);
    little(f,1,2); little(f,2,2); little(f,fs,4); little(f,4*fs,4); little(f,4,2); little(f,16,2);
    bytes("data"); little(f,dataSize,4);
    double sum=0; float finalPeak=0;
    for(int i=0;i<frames;++i) {
        const double fade=std::min(1.,(frames-1-i)/(.25*fs));
        for(int ch=0;ch<2;++ch) {
            const float sample=stereo[2*i+ch]*.81f/peak*fade;
            finalPeak=std::max(finalPeak,std::abs(sample)); sum+=sample*sample;
            little(f,static_cast<uint16_t>(static_cast<int16_t>(std::lround(32767*sample))),2);
        }
    }
    if(std::fclose(f)) return 4;
    std::printf("Reggae starts %.3fs; end %.3fs; peak %.2fdBFS, RMS %.2fdBFS\n",
        double(seam)/fs,double(frames)/fs,20*std::log10(finalPeak),10*std::log10(sum/stereo.size()));
}
