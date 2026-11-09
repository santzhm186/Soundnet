#pragma once
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>
#import "../ui/SNLocalization.h"
#include "../platform/ProjectEnvironment.hpp"

namespace tri::hardware {
inline NSString* stringProperty(AudioDeviceID device,AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress address{selector,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    CFStringRef value=nullptr;UInt32 size=sizeof(value);
    if(AudioObjectGetPropertyData(device,&address,0,nullptr,&size,&value)!=noErr||!value)return @"";
    return CFBridgingRelease(value);
}
inline bool hasChannels(AudioDeviceID device,bool input) {
    AudioObjectPropertyAddress address{kAudioDevicePropertyStreamConfiguration,
        input?kAudioDevicePropertyScopeInput:kAudioDevicePropertyScopeOutput,kAudioObjectPropertyElementMain};
    UInt32 size=0;
    if(AudioObjectGetPropertyDataSize(device,&address,0,nullptr,&size)!=noErr||size<sizeof(AudioBufferList))return false;
    std::vector<uint8_t> storage(size);
    auto* buffers=reinterpret_cast<AudioBufferList*>(storage.data());
    if(AudioObjectGetPropertyData(device,&address,0,nullptr,&size,buffers)!=noErr)return false;
    for(UInt32 i=0;i<buffers->mNumberBuffers;++i)if(buffers->mBuffers[i].mNumberChannels)return true;
    return false;
}
inline std::vector<AudioDeviceID> devices() {
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    UInt32 size=0;
    if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&address,0,nullptr,&size)!=noErr)return {};
    std::vector<AudioDeviceID> values(size/sizeof(AudioDeviceID));
    if(size&&AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,0,nullptr,&size,values.data())!=noErr)return {};
    return values;
}
inline AudioDeviceID resolve(const std::string& uid,bool input) {
    if(uid.empty()||uid=="system") {
        AudioObjectPropertyAddress address{input?kAudioHardwarePropertyDefaultInputDevice:kAudioHardwarePropertyDefaultOutputDevice,
            kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
        AudioDeviceID device=0;UInt32 size=sizeof(device);
        if(AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,0,nullptr,&size,&device)==noErr)return device;
        return 0;
    }
    NSString* requested=[NSString stringWithUTF8String:uid.c_str()];
    for(auto device:devices())
        if([stringProperty(device,kAudioDevicePropertyDeviceUID) isEqual:requested]&&hasChannels(device,input))return device;
    return 0;
}
inline NSError* select(AVAudioIONode* node,const std::string& uid,bool input) {
    // The system choice follows the OS default. Explicit UIDs are applied to
    // this application's HAL unit; the OS-wide default device is never changed.
    if(uid.empty()||uid=="system")return nil;
    AudioDeviceID device=resolve(uid,input);
    OSStatus status=device&&node.audioUnit?AudioUnitSetProperty(node.audioUnit,kAudioOutputUnitProperty_CurrentDevice,
        kAudioUnitScope_Global,0,&device,sizeof(device)):kAudio_ParamError;
    if(status==noErr)return nil;
    return [NSError errorWithDomain:@"Soundnet.AudioEnvironment" code:status
        userInfo:@{NSLocalizedDescriptionKey:[NSString stringWithFormat:SNLocalized(@"无法使用%@设备 %@（%d）。"),
            SNLocalized(input?@"输入":@"输出"),[NSString stringWithUTF8String:uid.c_str()],int(status)]}];
}
inline NSString* selectedUID(AVAudioIONode* node) {
    AudioDeviceID device=0;UInt32 size=sizeof(device);
    if(!node.audioUnit||AudioUnitGetProperty(node.audioUnit,kAudioOutputUnitProperty_CurrentDevice,
        kAudioUnitScope_Global,0,&device,&size)!=noErr)return @"";
    return stringProperty(device,kAudioDevicePropertyDeviceUID);
}
inline NSArray<NSDictionary*>* catalog() {
    NSMutableArray* result=[NSMutableArray new];
    for(auto device:devices()) {
        NSString* uid=stringProperty(device,kAudioDevicePropertyDeviceUID);
        if(!uid.length)continue;
        [result addObject:@{@"uid":uid,@"name":stringProperty(device,kAudioObjectPropertyName),
            @"input":@(hasChannels(device,true)),@"output":@(hasChannels(device,false))}];
    }
    return result;
}
}
