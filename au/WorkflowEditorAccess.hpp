#pragma once
#import <Foundation/Foundation.h>
#include "WorkflowProperties.hpp"
#include <atomic>
#include <chrono>

namespace ternaryAU {
// All methods except dirty/notify are called by the editor on its UI thread.
// A remote AUv2 proxy can filter vendor properties, so use standard ClassInfo there.
class EditorAccess {
    AudioUnit unit;
    bool remote=false;
    std::atomic<bool> dirty{true}, assetsDirty{true};
    NSDictionary *cachedAssets = @{};
    std::chrono::steady_clock::time_point assetsExpire{};
    std::chrono::steady_clock::time_point expires{};
    static void notify(void* context,AudioUnit,AudioUnitPropertyID,AudioUnitScope,AudioUnitElement) {
        static_cast<EditorAccess*>(context)->invalidate();
    }
    OSStatus readStandard() {
        CFPropertyListRef obj=nullptr;
        UInt32 size=sizeof(obj);
        OSStatus err=AudioUnitGetProperty(unit,kAudioUnitProperty_ClassInfo,kAudioUnitScope_Global,0,&obj,&size);
        if(err) return err;
        NSDictionary* dict=CFBridgingRelease(obj);
        NSArray* graph=dict[@"Ternary.Routes.1"];
        NSArray* positions=dict[@"Ternary.Layout.1"];
        if (![graph isKindOfClass:NSArray.class] || graph.count!=state.graph.size() ||
            ![positions isKindOfClass:NSArray.class] || positions.count!=state.layout.size())
            return kAudioUnitErr_InvalidPropertyValue;
        DisplayState next;
        for (NSUInteger i=0; i<graph.count; ++i) next.graph[i]=[graph[i] intValue];
        if(!tri::validRouting(next.graph)) return kAudioUnitErr_InvalidPropertyValue;
        for (NSUInteger i=0; i<positions.count; ++i) {
            next.layout[i]=[positions[i] floatValue];
            if(!tri::layout::valid(tri::layout::first+static_cast<int>(i),next.layout[i]))
                return kAudioUnitErr_InvalidPropertyValue;
        }
        NSDictionary *savedAssets=dict[@"Soundnet.Assets.1"] ?: @{};
        if (![savedAssets isKindOfClass:NSDictionary.class]) return kAudioUnitErr_InvalidPropertyValue;
        cachedAssets=savedAssets;
        state=next;
        return noErr;
    }
    OSStatus writeStandard(NSDictionary* delta) {
        // Keep a standard preset envelope so Apple's bridge forwards the dictionary.
        NSMutableDictionary* packet=[@{@"version":@0,@"type":@((UInt32)'aumu'),@"subtype":@((UInt32)'Tern'),
            @"manufacturer":@((UInt32)'Trny'),@"name":@"Workflow Edit",@"data":[NSData data]} mutableCopy];
        [packet addEntriesFromDictionary:delta];
        CFPropertyListRef obj=(__bridge CFPropertyListRef)packet;
        return AudioUnitSetProperty(unit,kAudioUnitProperty_ClassInfo,kAudioUnitScope_Global,0,&obj,sizeof(obj));
    }
public:
    DisplayState state{};
    OSStatus readError=noErr, writeError=noErr;
    explicit EditorAccess(AudioUnit au):unit(au) {
        UInt32 bytes=0;
        remote=AudioUnitGetPropertyInfo(unit,display,kAudioUnitScope_Global,0,&bytes,nullptr)!=noErr;
        AudioUnitAddPropertyListener(unit,kAudioUnitProperty_ClassInfo,notify,this);
        AudioUnitAddPropertyListener(unit,kAudioUnitProperty_PresentPreset,notify,this);
    }
    EditorAccess(const EditorAccess&)=delete;
    EditorAccess& operator=(const EditorAccess&)=delete;
    ~EditorAccess() {
        AudioUnitRemovePropertyListenerWithUserData(unit,kAudioUnitProperty_ClassInfo,notify,this);
        AudioUnitRemovePropertyListenerWithUserData(unit,kAudioUnitProperty_PresentPreset,notify,this);
    }
    void invalidate() {
        dirty.store(true,std::memory_order_relaxed);
        assetsDirty.store(true,std::memory_order_relaxed);
    }
    NSDictionary *readAssets() {
        if (remote) {
            if (refresh()) return nil;
        } else {
            const auto now=std::chrono::steady_clock::now();
            const bool changed=assetsDirty.exchange(false,std::memory_order_relaxed);
            if (changed || now>=assetsExpire) {
                CFDictionaryRef value=nullptr;
                UInt32 size=sizeof(value);
                readError=AudioUnitGetProperty(unit,assets,kAudioUnitScope_Global,0,&value,&size);
                if (readError) {
                    remote=true;
                    if (refresh(true)) return nil;
                } else cachedAssets=CFBridgingRelease(value);
                assetsExpire=now+std::chrono::seconds(2);
            }
        }
        return cachedAssets;
    }
    OSStatus setAssets(NSDictionary *value) {
        if (![value isKindOfClass:NSDictionary.class]) return writeError=kAudioUnitErr_InvalidPropertyValue;
        if (!remote) {
            CFDictionaryRef data=(__bridge CFDictionaryRef)value;
            writeError=AudioUnitSetProperty(unit,assets,kAudioUnitScope_Global,0,&data,sizeof(data));
            if (writeError==kAudioUnitErr_InvalidProperty) remote=true;
        }
        if (remote) writeError=writeStandard(@{@"Ternary.EditorCommand":@(editAssets),@"Soundnet.Assets.1":value});
        invalidate();
        return writeError;
    }
    OSStatus sendLocalMIDI(LocalMIDI event) {
        if (!remote) {
            writeError=AudioUnitSetProperty(unit,localMIDI,kAudioUnitScope_Global,0,&event,sizeof(event));
            if (writeError==kAudioUnitErr_InvalidProperty) remote=true;
        }
        if (remote) writeError=writeStandard(@{@"Ternary.EditorCommand":@(playLocalMIDI),
            @"Soundnet.LocalMIDI":[NSData dataWithBytes:&event length:sizeof(event)]});
        return writeError;
    }
    OSStatus refresh(bool force=false) {
        const auto now=std::chrono::steady_clock::now();
        const bool changed=dirty.exchange(false,std::memory_order_relaxed);
        if (!changed && !force && now<expires) return readError;
        if(!remote) {
            UInt32 bytes=sizeof(state);
            readError=AudioUnitGetProperty(unit,display,kAudioUnitScope_Global,0,&state,&bytes);
            if(readError) remote=true;
        }
        if(remote) readError=readStandard();
        // Hosts can restore a project without notifying a custom editor. Refresh the
        // compact full state periodically as a backup, not once per knob or render block.
        expires=now+std::chrono::seconds(2);
        return readError;
    }
    OSStatus setGraph(const tri::Routing& graph) {
        if(!tri::validRouting(graph)) return writeError=kAudioUnitErr_InvalidPropertyValue;
        if(!remote) {
            writeError=AudioUnitSetProperty(unit,routes,kAudioUnitScope_Global,0,graph.data(),sizeof(graph));
            if(writeError==kAudioUnitErr_InvalidProperty) remote=true;
        }
        if(remote) {
            NSMutableArray* values=[NSMutableArray arrayWithCapacity:graph.size()];
            for(int src:graph) [values addObject:@(src)];
            writeError=writeStandard(@{@"Ternary.EditorCommand":@(tri::layout::commitRouting),@"Ternary.Routes.1":values});
        }
        invalidate();
        return writeError;
    }
    OSStatus setPosition(int id,float value) {
        if(!tri::layout::valid(id,value)) return writeError=kAudioUnitErr_InvalidPropertyValue;
        if(!remote) {
            writeError=AudioUnitSetProperty(unit,layoutProperty(id),kAudioUnitScope_Global,0,&value,sizeof(value));
            if(writeError==kAudioUnitErr_InvalidProperty) remote=true;
        }
        if(remote) writeError=writeStandard(@{@"Ternary.EditorCommand":@(id),@"Ternary.Coordinate":@(value)});
        invalidate();
        return writeError;
    }
};
}
