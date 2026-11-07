#include "../src/AudioFile.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
        auto bytes=makeAiff();
        AudioSample sample;
        assert(audiofile::parse(bytes.data(),bytes.size(),sample,why));
        assert(std::abs(sample.rate-48000)<.01 && sample.channels==1 && sample.frames()==4);
        assert(std::abs(sample.samples[0]-.5f)<1e-4);
        assert(std::abs(sample.samples[2]+.5f)<1e-4);
        puts("AIFF PCM16 decodes");
    }
    {
        auto bytes=makeWav();
        bytes.resize(30);
        AudioSample sample;
        assert(!audiofile::parse(bytes.data(),bytes.size(),sample,why));
        puts("truncated audio is rejected");
    }
    puts("PASS: WAV and AIFF audio import.");
}
