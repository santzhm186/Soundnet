#include "../src/AudioFile.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

using namespace tri;

static void put16(std::vector<uint8_t>& out,size_t offset,uint16_t value) {
    out[offset]=uint8_t(value);out[offset+1]=uint8_t(value>>8);
}
static void put32(std::vector<uint8_t>& out,size_t offset,uint32_t value) {
    out[offset]=uint8_t(value);out[offset+1]=uint8_t(value>>8);
    out[offset+2]=uint8_t(value>>16);out[offset+3]=uint8_t(value>>24);
}
static void put16be(std::vector<uint8_t>& out,size_t offset,uint16_t value) {
    out[offset]=uint8_t(value>>8);out[offset+1]=uint8_t(value);
}
static void put32be(std::vector<uint8_t>& out,size_t offset,uint32_t value) {
    out[offset]=uint8_t(value>>24);out[offset+1]=uint8_t(value>>16);
    out[offset+2]=uint8_t(value>>8);out[offset+3]=uint8_t(value);
}

static std::vector<uint8_t> makeWav() {
    std::vector<uint8_t> out(44+8);
    std::memcpy(out.data(),"RIFF",4);put32(out,4,36+8);std::memcpy(out.data()+8,"WAVEfmt ",8);
    put32(out,16,16);put16(out,20,1);put16(out,22,1);put32(out,24,48000);
    put32(out,28,96000);put16(out,32,2);put16(out,34,16);
    std::memcpy(out.data()+36,"data",4);put32(out,40,8);
    put16(out,44,16384);put16(out,46,0);put16(out,48,uint16_t(int16_t(-16384)));put16(out,50,0);
    return out;
}
static std::vector<uint8_t> makeWav24() {
    std::vector<uint8_t> out(44+6);
    std::memcpy(out.data(),"RIFF",4);put32(out,4,36+6);std::memcpy(out.data()+8,"WAVEfmt ",8);
    put32(out,16,16);put16(out,20,1);put16(out,22,1);put32(out,24,48000);
    put32(out,28,144000);put16(out,32,3);put16(out,34,24);
    std::memcpy(out.data()+36,"data",4);put32(out,40,6);
    out[44]=0;out[45]=0;out[46]=0x40;      // 0x400000 = 0.5
    out[47]=0;out[48]=0;out[49]=0xc0;      // 0xc00000 = -0.5
    return out;
}
static std::vector<uint8_t> makeWavFloat() {
    std::vector<uint8_t> out(44+8);
    std::memcpy(out.data(),"RIFF",4);put32(out,4,36+8);std::memcpy(out.data()+8,"WAVEfmt ",8);
    put32(out,16,16);put16(out,20,3);put16(out,22,1);put32(out,24,48000);
    put32(out,28,192000);put16(out,32,4);put16(out,34,32);
    std::memcpy(out.data()+36,"data",4);put32(out,40,8);
    const float values[2]={.25f,-.5f};std::memcpy(out.data()+44,values,8);
    return out;
}
static std::vector<uint8_t> makeAifcSowt() {
    std::vector<uint8_t> out(70);
    std::memcpy(out.data(),"FORM",4);put32be(out,4,58);std::memcpy(out.data()+8,"AIFC",4);
    std::memcpy(out.data()+12,"COMM",4);put32be(out,16,22);
    put16be(out,20,1);put32be(out,22,2);put16be(out,26,16);
    const uint8_t rate[10]={0x40,0x0e,0xbb,0x80,0,0,0,0,0,0};
    std::memcpy(out.data()+28,rate,10);std::memcpy(out.data()+38,"sowt",4);
    std::memcpy(out.data()+42,"SSND",4);put32be(out,46,12);
    put32be(out,50,0);put32be(out,54,0);
    put16(out,58,16384);put16(out,60,uint16_t(int16_t(-16384)));
    return out;
}
static std::vector<uint8_t> makeWav32() {
    std::vector<uint8_t> out(44+8);
    std::memcpy(out.data(),"RIFF",4);put32(out,4,36+8);std::memcpy(out.data()+8,"WAVEfmt ",8);
    put32(out,16,16);put16(out,20,1);put16(out,22,1);put32(out,24,48000);
    put32(out,28,192000);put16(out,32,4);put16(out,34,32);
    std::memcpy(out.data()+36,"data",4);put32(out,40,8);
    put32(out,44,0x40000000u);put32(out,48,0xc0000000u);
    return out;
}
static std::vector<uint8_t> makeAiff24() {
    std::vector<uint8_t> out(64);
    std::memcpy(out.data(),"FORM",4);put32be(out,4,56);std::memcpy(out.data()+8,"AIFF",4);
    std::memcpy(out.data()+12,"COMM",4);put32be(out,16,18);
    put16be(out,20,1);put32be(out,22,2);put16be(out,26,24);
    const uint8_t rate[10]={0x40,0x0e,0xbb,0x80,0,0,0,0,0,0};
    std::memcpy(out.data()+28,rate,10);
    std::memcpy(out.data()+38,"SSND",4);put32be(out,42,14);
    put32be(out,46,0);put32be(out,50,0);
    out[54]=0x40;out[55]=0;out[56]=0;out[57]=0xc0;out[58]=0;out[59]=0;
    return out;
}
static std::vector<uint8_t> makeAifcFloat(bool little) {
    std::vector<uint8_t> out(70);
    std::memcpy(out.data(),"FORM",4);put32be(out,4,58);std::memcpy(out.data()+8,"AIFC",4);
    std::memcpy(out.data()+12,"COMM",4);put32be(out,16,22);
    put16be(out,20,1);put32be(out,22,2);put16be(out,26,32);
    const uint8_t rate[10]={0x40,0x0e,0xbb,0x80,0,0,0,0,0,0};
    std::memcpy(out.data()+28,rate,10);
    std::memcpy(out.data()+38,little?"FL32":"fl32",4);
    std::memcpy(out.data()+42,"SSND",4);put32be(out,46,16);
    put32be(out,50,0);put32be(out,54,0);
    const float values[2]={.35f,-.45f};
    std::memcpy(out.data()+58,values,8);
    if(!little) {
        for(int i=0;i<2;++i) std::reverse(out.begin()+58+i*4,out.begin()+62+i*4);
    }
    return out;
}
static std::vector<uint8_t> makeWav8() {
    std::vector<uint8_t> out(48);
    std::memcpy(out.data(),"RIFF",4);put32(out,4,40);std::memcpy(out.data()+8,"WAVEfmt ",8);
    put32(out,16,16);put16(out,20,1);put16(out,22,1);put32(out,24,8000);
    put32(out,28,8000);put16(out,32,1);put16(out,34,8);
    std::memcpy(out.data()+36,"data",4);put32(out,40,4);
    out[44]=0;out[45]=128;out[46]=255;out[47]=128;
    return out;
}
static std::vector<uint8_t> makeAiff() {
    std::vector<uint8_t> out(62);
    std::memcpy(out.data(),"FORM",4);put32be(out,4,54);std::memcpy(out.data()+8,"AIFF",4);
    std::memcpy(out.data()+12,"COMM",4);put32be(out,16,18);
    put16be(out,20,1);put32be(out,22,4);put16be(out,26,16);
    const uint8_t rate[10]={0x40,0x0e,0xbb,0x80,0,0,0,0,0,0};
    std::memcpy(out.data()+28,rate,10);
    std::memcpy(out.data()+38,"SSND",4);put32be(out,42,16);
    put32be(out,46,0);put32be(out,50,0);
    put16be(out,54,16384);put16be(out,56,0);put16be(out,58,uint16_t(int16_t(-16384)));put16be(out,60,0);
    return out;
}

int main() {
    std::string why;
    {
        auto bytes=makeWav();
        AudioSample sample;
        assert(audiofile::parse(bytes.data(),bytes.size(),sample,why));
        assert(sample.rate==48000 && sample.channels==1 && sample.frames()==4);
        assert(std::abs(sample.samples[0]-.5f)<1e-4);
        assert(std::abs(sample.samples[2]+.5f)<1e-4);
        puts("WAV PCM16 decodes");
    }
    {
        auto bytes=makeWav24();
        AudioSample sample;
        assert(audiofile::parse(bytes.data(),bytes.size(),sample,why));
        assert(sample.frames()==2);
        assert(std::abs(sample.samples[0]-.5f)<1e-4);
        assert(std::abs(sample.samples[1]+.5f)<1e-4);
        puts("WAV PCM24 decodes");
    }
    {
        auto bytes=makeWavFloat();
        AudioSample sample;
        assert(audiofile::parse(bytes.data(),bytes.size(),sample,why));
        assert(std::abs(sample.samples[0]-.25f)<1e-6);
        assert(std::abs(sample.samples[1]+.5f)<1e-6);
        puts("WAV float32 decodes");
    }
    {
        auto bytes=makeWav32();
        AudioSample sample;
        assert(audiofile::parse(bytes.data(),bytes.size(),sample,why));
        assert(std::abs(sample.samples[0]-.5f)<1e-4);
        assert(std::abs(sample.samples[1]+.5f)<1e-4);
        puts("WAV PCM32 decodes");
    }
    {
        auto bytes=makeAiff24();
        AudioSample sample;
        assert(audiofile::parse(bytes.data(),bytes.size(),sample,why));
        assert(sample.frames()==2);
        assert(std::abs(sample.samples[0]-.5f)<1e-4);
        assert(std::abs(sample.samples[1]+.5f)<1e-4);
        puts("AIFF PCM24 decodes");
    }
    {
        auto bytes=makeAifcFloat(false);
        AudioSample sample;
        assert(audiofile::parse(bytes.data(),bytes.size(),sample,why));
        assert(std::abs(sample.samples[0]-.35f)<1e-6);
        assert(std::abs(sample.samples[1]+.45f)<1e-6);
        puts("AIFC fl32 decodes");
    }
    {
        auto bytes=makeAifcFloat(true);
        AudioSample sample;
        assert(audiofile::parse(bytes.data(),bytes.size(),sample,why));
        assert(std::abs(sample.samples[0]-.35f)<1e-6);
        assert(std::abs(sample.samples[1]+.45f)<1e-6);
        puts("AIFC FL32 decodes");
    }
    {
        auto bytes=makeAifcSowt();
        AudioSample sample;
        assert(audiofile::parse(bytes.data(),bytes.size(),sample,why));
        assert(sample.frames()==2);
        assert(std::abs(sample.samples[0]-.5f)<1e-4);
        assert(std::abs(sample.samples[1]+.5f)<1e-4);
        puts("AIFC sowt decodes");
    }
    {
        auto bytes=makeWav8();
        AudioSample sample;
        assert(audiofile::parse(bytes.data(),bytes.size(),sample,why));
        assert(sample.frames()==4);
        assert(std::abs(sample.samples[0]+1.f)<1e-4);
        assert(std::abs(sample.samples[1])<1e-4);
        assert(std::abs(sample.samples[3]-.0f)<.01f);
        puts("WAV PCM8 decodes");
    }
    {
        auto bytes=makeAiff();
        AudioSample sample;
        assert(audiofile::parse(bytes.data(),bytes.size(),sample,why));
        assert(std::abs(sample.rate-48000)<.01 && sample.channels==1 && sample.frames()==4);
        assert(std::abs(sample.samples[0]-.5f)<1e-4);
        assert(std::abs(sample.samples[2]+.5f)<1e-4);
        puts("AIFF PCM16 decodes");
    }
    {
        auto bytes=makeWavFloat();
        const float bad=std::nanf("");
        std::memcpy(bytes.data()+44,&bad,4);
        AudioSample sample;
        assert(!audiofile::parse(bytes.data(),bytes.size(),sample,why));
        puts("non-finite audio samples are rejected");
    }
    {
        auto bytes=makeWav();
        bytes.resize(30);
        AudioSample sample;
        assert(!audiofile::parse(bytes.data(),bytes.size(),sample,why));
        puts("truncated audio is rejected");
    }
    puts("PASS: WAV, AIFF and AIFC audio import.");
}
