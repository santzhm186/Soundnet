#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
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
// Writes interleaved stereo 16-bit PCM. Refuses empty or mismatched channels.
inline bool write(const std::string& path,const std::vector<float>& left,
                  const std::vector<float>& right,double rate,std::string& why) {
    if(left.empty()||left.size()!=right.size()){why="没有可导出的音频。";return false;}
    if(rate<1000||rate>384000){why="采样率超出范围。";return false;}
    if(left.size()>0x1fffffffu){why="音频过长。";return false;}
    uint32_t frames=uint32_t(left.size());
    uint32_t dataBytes=frames*4;
    std::vector<uint8_t> header;
    header.reserve(44);
    const char riff[]="RIFF";header.insert(header.end(),riff,riff+4);
    put32(header,36+dataBytes);
    const char wave[]="WAVEfmt ";header.insert(header.end(),wave,wave+8);
    put32(header,16);put16(header,1);put16(header,2);
    put32(header,uint32_t(std::lround(rate)));put32(header,uint32_t(std::lround(rate))*4);
    put16(header,4);put16(header,16);
    const char data[]="data";header.insert(header.end(),data,data+4);
    put32(header,dataBytes);
    std::ofstream out(path,std::ios::binary);
    if(!out){why="无法创建 WAV 文件。";return false;}
    out.write(reinterpret_cast<const char*>(header.data()),std::streamsize(header.size()));
    std::vector<int16_t> block;
    block.reserve(4096*2);
    for(size_t i=0;i<left.size();++i) {
        float l=std::max(-1.f,std::min(1.f,left[i]));
        float r=std::max(-1.f,std::min(1.f,right[i]));
        block.push_back(int16_t(std::lround(l*32767.f)));
        block.push_back(int16_t(std::lround(r*32767.f)));
        if(block.size()>=4096*2||i+1==left.size()) {
            out.write(reinterpret_cast<const char*>(block.data()),std::streamsize(block.size()*sizeof(int16_t)));
            block.clear();
        }
    }
    if(!out){why="写入 WAV 文件失败。";return false;}
    return true;
}
} // namespace tri::wav
