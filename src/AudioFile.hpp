#pragma once
// Minimal, dependency-free WAV/AIFF reader used when importing sampler audio.
// The graph document embeds decoded float32 PCM, so decoding must also work in
// the plain C++ test harness (no AVFoundation / CoreAudio).
#include "AudioSample.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace tri {
namespace audiofile {

constexpr size_t maxFileBytes=64*1024*1024;
constexpr size_t maxSamples=4*1024*1024; // 16 MB float32; ~41 s stereo @48 kHz

inline uint16_t le16(const uint8_t *p) { return uint16_t(p[0]|(p[1]<<8)); }
inline uint32_t le32(const uint8_t *p) {
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
inline uint16_t be16(const uint8_t *p) { return uint16_t((p[0]<<8)|p[1]); }
inline uint32_t be32(const uint8_t *p) {
    return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|uint32_t(p[3]);
}
inline double extended80(const uint8_t *p) {
    const bool negative=(p[0]&0x80)!=0;
    const int exponent=int((p[0]&0x7f)<<8)|p[1];
    uint64_t mantissa=0;
    for(int i=2;i<10;++i)mantissa=(mantissa<<8)|p[i];
    if(exponent==0 && mantissa==0)return 0;
    if(exponent==0x7fff)return 0;
    double value=std::ldexp(double(mantissa),exponent-16383-63);
    return negative?-value:value;
}
inline bool fail(std::string& why,const char *message) { why=message; return false; }

inline float pcmValue(const uint8_t *p,int bytes,bool little,bool floating,bool unsigned8) {
    if(floating) {
        uint32_t bits=little?le32(p):be32(p);
        float value=0;std::memcpy(&value,&bits,sizeof(value));return value;
    }
    if(bytes==1) {
        if(unsigned8)return (float(p[0])-128.f)/128.f;
        return float(int8_t(p[0]))/128.f;
    }
    if(bytes==2) {
        int16_t value=little?int16_t(le16(p)):int16_t(be16(p));
        return float(value)/32768.f;
    }
    if(bytes==3) {
        int32_t value=little
            ? int32_t(uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16))
            : int32_t((uint32_t(p[0])<<16)|(uint32_t(p[1])<<8)|uint32_t(p[2]));
        if(value&0x800000)value|=int32_t(0xff000000u);
        return float(value)/8388608.f;
    }
    if(bytes==4) {
        int32_t value=little?int32_t(le32(p)):int32_t(be32(p));
        return float(double(value)/2147483648.0);
    }
    return 0;
}

inline bool parseWav(const uint8_t *data,size_t size,tri::AudioSample& out,std::string& why) {
    size_t position=12;
    bool haveFormat=false,haveData=false;
    uint16_t format=0,channels=0,bits=0,validBits=0;
    bool extensible=false;
    uint32_t rate=0,blockAlign=0;
    size_t dataOffset=0,dataSize=0;
    while(position+8<=size) {
        const uint8_t *id=data+position;
        uint32_t chunk=le32(data+position+4);
        size_t body=position+8;
        if(body>size || chunk>size-body)return fail(why,"WAV 数据块不完整。");
        if(!std::memcmp(id,"fmt ",4)) {
            if(chunk<16)return fail(why,"WAV 格式块过短。");
            format=le16(data+body);channels=le16(data+body+2);
            rate=le32(data+body+4);blockAlign=le16(data+body+12);bits=le16(data+body+14);
            validBits=bits;
            if(format==0xfffe) {
                static const uint8_t guidTail[14]={0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
                if(chunk<40 || std::memcmp(data+body+26,guidTail,14))return fail(why,"WAV 扩展格式块无效。");
                extensible=true;
                format=le16(data+body+24);
                validBits=le16(data+body+18);
                if(!validBits)validBits=bits;
            }
            haveFormat=true;
        } else if(!std::memcmp(id,"data",4)) {
            dataOffset=body;dataSize=chunk;haveData=true;
        }
        position=body+((size_t(chunk)+1)&~size_t(1));
    }
    if(!haveFormat || !haveData)return fail(why,"WAV 缺少格式或音频数据块。");
    if(channels<1 || channels>2)return fail(why,"目前只支持单声道或双声道音频。");
    if(rate<1000 || rate>384000)return fail(why,"音频采样率需在 1 kHz–384 kHz 之间。");
    const bool floating=format==3;
    if(!floating && format!=1)return fail(why,"暂不支持该 WAV 编码，请使用 PCM 或 Float32。");
    if(floating && bits!=32)return fail(why,"目前只支持 32 位浮点 WAV。");
    if(!floating && bits!=8 && bits!=16 && bits!=24 && bits!=32)return fail(why,"仅支持 8/16/24/32 位 PCM。");
    if(validBits>bits)return fail(why,"WAV 有效位数超过容器位数。");
    if(validBits!=bits && !(bits==32 && validBits==24))return fail(why,"仅支持 24 位有效数据的 32 位 WAV 容器。");
    const int bytes=int(bits/8);
    if(blockAlign && blockAlign!=uint32_t(bytes*channels))return fail(why,"WAV 的 block align 与格式不符。");
    const size_t stride=size_t(bytes)*channels;
    if(!stride || dataSize%stride)return fail(why,"WAV 音频数据长度不完整。");
    const size_t frames=dataSize/stride;
    if(!frames)return fail(why,"音频文件没有采样数据。");
    if(frames>maxSamples/size_t(channels))return fail(why,"音频过长，请导入 20 秒以内的素材。");
    out.samples.resize(frames*size_t(channels));
    for(size_t frame=0;frame<frames;++frame)for(int c=0;c<int(channels);++c) {
        const uint8_t *source=data+dataOffset+frame*stride+size_t(c)*size_t(bytes);
        if(extensible && !floating && bits==32 && validBits==24) {
            const int32_t value=int32_t(le32(source))>>8;
            out.samples[frame*size_t(channels)+size_t(c)]=float(double(value)/8388608.0);
        } else {
            out.samples[frame*size_t(channels)+size_t(c)]=pcmValue(source,bytes,true,floating,true);
        }
    }
    out.channels=channels;out.rate=rate;
    return true;
}

inline bool parseAiff(const uint8_t *data,size_t size,tri::AudioSample& out,std::string& why) {
    const bool aifc=!std::memcmp(data+8,"AIFC",4);
    size_t position=12;
    bool haveFormat=false,haveSound=false;
    uint16_t channels=0,bits=0;
    uint32_t declaredFrames=0;
    double rate=0;
    size_t dataOffset=0,dataSize=0;
    bool little=false,floating=false;
    while(position+8<=size) {
        const uint8_t *id=data+position;
        uint32_t chunk=be32(data+position+4);
        size_t body=position+8;
        if(body>size || chunk>size-body)return fail(why,"AIFF 数据块不完整。");
        if(!std::memcmp(id,"COMM",4)) {
            if(chunk<18)return fail(why,"AIFF 公共块过短。");
            channels=be16(data+body);
            declaredFrames=be32(data+body+2);
            bits=be16(data+body+6);
            rate=extended80(data+body+8);
            if(aifc) {
                if(chunk<22)return fail(why,"AIFC 压缩类型缺失。");
                const uint8_t *compression=data+body+18;
                floating=!std::memcmp(compression,"fl32",4) || !std::memcmp(compression,"FL32",4);
                little=!std::memcmp(compression,"sowt",4) || !std::memcmp(compression,"FL32",4);
                if(!floating && std::memcmp(compression,"NONE",4) && std::memcmp(compression,"sowt",4))
                    return fail(why,"暂不支持该 AIFF 压缩格式，请使用 PCM。");
            }
            haveFormat=true;
        } else if(!std::memcmp(id,"SSND",4)) {
            if(chunk<8)return fail(why,"AIFF 声音块过短。");
            uint32_t offset=be32(data+body);
            if(uint64_t(offset)+8>chunk)return fail(why,"AIFF 声音块偏移无效。");
            dataOffset=body+8+offset;dataSize=chunk-8-offset;haveSound=true;
        }
        position=body+((size_t(chunk)+1)&~size_t(1));
    }
    if(!haveFormat || !haveSound)return fail(why,"AIFF 缺少公共块或声音数据。");
    if(channels<1 || channels>2)return fail(why,"目前只支持单声道或双声道音频。");
    if(rate<1000 || rate>384000)return fail(why,"音频采样率需在 1 kHz–384 kHz 之间。");
    if(floating && bits!=32)return fail(why,"目前只支持 32 位浮点 AIFF。");
    if(!floating && bits!=8 && bits!=16 && bits!=24 && bits!=32)return fail(why,"仅支持 8/16/24/32 位 PCM。");
    const int bytes=int(bits/8);
    const size_t stride=size_t(bytes)*channels;
    if(!stride || dataSize%stride)return fail(why,"AIFF 音频数据长度不完整。");
    size_t frames=dataSize/stride;
    if(declaredFrames && declaredFrames<frames)frames=declaredFrames;
    if(!frames)return fail(why,"音频文件没有采样数据。");
    if(frames>maxSamples/size_t(channels))return fail(why,"音频过长，请导入 20 秒以内的素材。");
    out.samples.resize(frames*size_t(channels));
    for(size_t frame=0;frame<frames;++frame)for(int c=0;c<int(channels);++c)
        out.samples[frame*size_t(channels)+size_t(c)]=
            pcmValue(data+dataOffset+frame*stride+size_t(c)*size_t(bytes),bytes,little,floating,false);
    out.channels=channels;out.rate=rate;
    return true;
}

inline bool parse(const uint8_t *data,size_t size,tri::AudioSample& out,std::string& why) {
    why.clear();
    if(!data || size<12)return fail(why,"音频文件过短。");
    if(!std::memcmp(data,"RIFF",4) && !std::memcmp(data+8,"WAVE",4)) {
        if(!parseWav(data,size,out,why))return false;
    } else if(!std::memcmp(data,"FORM",4) &&
              (!std::memcmp(data+8,"AIFF",4) || !std::memcmp(data+8,"AIFC",4))) {
        if(!parseAiff(data,size,out,why))return false;
    } else {
        return fail(why,"目前支持 WAV 与 AIFF/AIFC 音频文件。");
    }
    if(!out.valid())return fail(why,"解码后的音频参数无效。");
    for(float value:out.samples)
        if(!std::isfinite(value))return fail(why,"音频包含无效的采样值。");
    return true;
}
} // namespace audiofile
} // namespace tri
