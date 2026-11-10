#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioToolbox/AUCocoaUIView.h>
#import "../ui/TriEditor.h"
#include "WorkflowEditorAccess.hpp"
#import "../platform/NodePackageLoader.h"
#include <memory>
@interface TrnyTernaryViewFactory : NSObject<AUCocoaUIBase>
@end
@implementation TrnyTernaryViewFactory
- (unsigned)interfaceVersion { return 0; }
- (NSString *)description { return @"Soundnet"; }
- (NSView *)uiViewForAudioUnit:(AudioUnit)unit withSize:(NSSize)size {
    auto edit=std::make_shared<tri::RouteEdit>();
    auto cache=std::make_shared<ternaryAU::EditorAccess>(unit);
    auto readGraph=[cache] {cache->refresh(true); return cache->state.graph;};
    const auto loaded=tri::refreshNodePackages(tri::defaultNodePackageRoots(tri::nodePlatformBundle()),true);
    if(!loaded.ok)return nil;
    NSView* editor=TRICreateEditor(^float(int id) {
        if (tri::layout::contains(id)) {
            cache->refresh();
            return cache->state.layout[id-tri::layout::first];
        }
        if (tri::routeParam(id)) {
            cache->refresh();
            return cache->state.graph[tri::routeIndex(id)];
        }
        if(id<0 || id>=tri::ParamCount) return 0;
        AudioUnitParameterValue value = tri::params[id].def;
        AudioUnitGetParameter(unit, id, kAudioUnitScope_Global, 0, &value);
        return value;
    }, ^(int id, float value) {
        if (tri::layout::contains(id)) {
            cache->setPosition(id,value);
            return;
        }
        if (edit->handle(id,value,readGraph,[cache](const tri::Routing& graph) {
            cache->setGraph(graph);
        })) return;
        if(id<0 || id>=tri::ParamCount) return;
        AudioUnitEvent event = {};
        event.mArgument.mParameter = {unit, static_cast<UInt32>(id), kAudioUnitScope_Global, 0};
        event.mEventType = kAudioUnitEvent_BeginParameterChangeGesture;
        AUEventListenerNotify(nullptr,nullptr,&event);
        AUParameterSet(nullptr,nullptr,&event.mArgument.mParameter,value,0);
        event.mEventType = kAudioUnitEvent_EndParameterChangeGesture;
        AUEventListenerNotify(nullptr,nullptr,&event);
    }, ^(unsigned char status,unsigned char a,unsigned char b) {
        cache->sendLocalMIDI({status,a,b});
    }, nil,nil, ^NSString *{
        OSStatus err=cache->writeError ? cache->writeError : cache->readError;
        return err ? [NSString stringWithFormat:@"工作流访问失败 (%d)",(int)err] : @"Audio Unit · Host sync";
    }, ^NSDictionary *{ return cache->readAssets(); },
       ^BOOL(NSDictionary *assets){ return cache->setAssets(assets)==noErr; },
       ^BOOL {
           UInt32 enabled=1,size=sizeof(enabled);
           AudioUnitGetProperty(unit,ternaryAU::processing,kAudioUnitScope_Global,0,&enabled,&size);
           return enabled!=0;
       }, ^(BOOL enabled) {
           UInt32 value=enabled?1:0;
           cache->writeError=AudioUnitSetProperty(unit,ternaryAU::processing,kAudioUnitScope_Global,0,&value,sizeof(value));
       }, nil, ^NSDictionary * {
           AudioStreamBasicDescription format{};UInt32 size=sizeof(format);
           AudioUnitGetProperty(unit,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Output,0,&format,&size);
           return @{@"actualRate":@(format.mSampleRate),@"deviceRate":@(format.mSampleRate),
               @"processingPrecision":@32,@"hostManaged":@YES,@"devices":@[]};
       }, ^NSDictionary *(NSDictionary *command) {
           NSMutableDictionary *request=[command mutableCopy];
           request[@"request"]=NSUUID.UUID.UUIDString;
           CFDictionaryRef raw=(__bridge CFDictionaryRef)request;
           if(AudioUnitSetProperty(unit,ternaryAU::midiCapture,kAudioUnitScope_Global,0,&raw,sizeof(raw))!=noErr)
               return @{};
           CFDictionaryRef response=nullptr;
           UInt32 size=sizeof(response);
           if(AudioUnitGetProperty(unit,ternaryAU::midiCapture,kAudioUnitScope_Global,0,&response,&size)!=noErr)
               return @{};
           NSDictionary *reply=CFBridgingRelease(response);
           return [reply[@"request"] isEqual:request[@"request"]]?reply:@{};
       });
    __weak NSView* weakEditor=editor;
    TRISetNodeLibraryRefresh(editor,^(void (^completed)(NSString*)) {
        if(cache->refreshNodes()!=noErr){completed(@"宿主无法刷新节点库。");return;}
        auto refreshed=tri::refreshNodePackages(tri::defaultNodePackageRoots(tri::nodePlatformBundle()),true);
        NSString* why=nil;
        if(refreshed.ok&&weakEditor&&!TRIRefreshNodeCatalog(weakEditor,&why))
            completed(why ?: @"节点已加载，工程包含不可用节点。");
        else completed([NSString stringWithUTF8String:refreshed.message.c_str()]);
    });
    return editor;
}
@end
