#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioToolbox/AUCocoaUIView.h>
#import "../ui/TriEditor.h"
#include "WorkflowEditorAccess.hpp"
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
    return TRICreateEditor(^float(int id) {
        if (tri::layout::contains(id)) {
            cache->refresh();
            return cache->state.layout[id-tri::layout::first];
        }
        if (tri::routeParam(id)) {
            cache->refresh();
            return cache->state.graph[id-tri::RouteArpIn];
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
        MusicDeviceMIDIEvent(unit,status,a,b,0);
    }, nil,nil, ^NSString *{
        OSStatus err=cache->writeError ? cache->writeError : cache->readError;
        return err ? [NSString stringWithFormat:@"工作流访问失败 (%d)",(int)err] : @"Audio Unit · Host sync";
    });
}
@end
