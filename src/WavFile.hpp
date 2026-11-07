#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace tri::wav {
inline void put16(std::vector<uint8_t>& bytes,uint16_t value) {
    bytes.push_back(uint8_t(value));bytes.push_back(uint8_t(value>>8));
}
inline void put32(std::vector<uint8_t>& bytes,uint32_t value) {
    bytes.push_back(uint8_t(value));bytes.push_back(uint8_t(value>>8));
    bytes.push_back(uint8_t(value>>16));bytes.push_back(uint8_t(value>>24));
}
// Writes interleaved stereo PCM (16/24-bit) or 32-bit float. Refuses empty or mismatched channels.
inline bool write(const std::string& path,const std::vector<float>& left,
                  const std::vector<float>& right,double rate,std::string& why,int bits=16) {
    if(left.empty()||left.size()!=right.size()){why="没有可导出的音频。";return false;}
    if(rate<1000||rate>384000){why="采样率超出范围。";return false;}
    if(bits!=16&&bits!=24&&bits!=32){why="位深必须是 16、24 或 32（浮点）。";return false;}
    const uint32_t bytesPerSample=uint32_t(bits/8);
    const uint32_t blockAlign=2*bytesPerSample;
    if(left.size()>(0xffffffffu-44u)/blockAlign){why="音频过长。";return false;}
    const uint32_t frames=uint32_t(left.size());
    const uint32_t dataBytes=frames*blockAlign;
    std::vector<uint8_t> header;
    header.reserve(44);
    const char riff[]="RIFF";header.insert(header.end(),riff,riff+4);
    put32(header,36+dataBytes);
    const char wave[]="WAVEfmt ";header.insert(header.end(),wave,wave+8);
    const uint16_t format=bits==32?3:1;
    put32(header,16);put16(header,format);put16(header,2);
    const uint32_t sampleRate=uint32_t(std::lround(rate));
    put32(header,sampleRate);put32(header,sampleRate*blockAlign);
    put16(header,uint16_t(blockAlign));put16(header,uint16_t(bits));
    const char data[]="data";header.insert(header.end(),data,data+4);
    put32(header,dataBytes);
    std::ofstream out(path,std::ios::binary);
    if(!out){why="无法创建 WAV 文件。";return false;}
    out.write(reinterpret_cast<const char*>(header.data()),std::streamsize(header.size()));
    std::vector<uint8_t> block;
    block.reserve(4096*blockAlign);
    for(size_t i=0;i<left.size();++i) {
        const float samples[2]={std::max(-1.f,std::min(1.f,left[i])),std::max(-1.f,std::min(1.f,right[i]))};
        for(float sample:samples) {
            if(bits==16) {
                const int16_t value=int16_t(std::lround(sample*32767.f));
                const uint16_t raw=uint16_t(value);
                block.push_back(uint8_t(raw&0xff));block.push_back(uint8_t(raw>>8));
            } else if(bits==24) {
                const int32_t value=int32_t(std::lround(sample*8388607.f));
                block.push_back(uint8_t(value&0xff));
                block.push_back(uint8_t((value>>8)&0xff));
                block.push_back(uint8_t((value>>16)&0xff));
            } else {
                uint32_t raw=0;std::memcpy(&raw,&sample,sizeof(raw));
                block.push_back(uint8_t(raw&0xff));block.push_back(uint8_t((raw>>8)&0xff));
                block.push_back(uint8_t((raw>>16)&0xff));block.push_back(uint8_t((raw>>24)&0xff));
            }
        }
        if(block.size()>=4096*blockAlign||i+1==left.size()) {
            out.write(reinterpret_cast<const char*>(block.data()),std::streamsize(block.size()));
            block.clear();
        }
    }
    if(!out){why="写入 WAV 文件失败。";return false;}
    return true;
}

} // namespace tri::wav
