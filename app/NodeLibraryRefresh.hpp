#pragma once
#import <Cocoa/Cocoa.h>
#import "../ui/TriEditor.h"
#import "../ui/SNLocalization.h"
#import "../platform/NodePackageLoader.h"
namespace tri::node_refresh {
inline void refresh(NSView* editor,BOOL (^recordingAudio)(void),void (^completed)(NSString*)){
    if(recordingAudio&&recordingAudio()){completed(SNLocalized(@"请先完成录制，再刷新节点。"));return;}
    const auto refreshed=refreshNodePackages(defaultNodePackageRoots(NSBundle.mainBundle),true);
    if(!refreshed.ok){completed([NSString stringWithUTF8String:refreshed.message.c_str()]);return;}
    NSString* why=nil;
    if(refreshed.changed&&!TRIRefreshNodeCatalog(editor,&why)){
        completed(why ?: SNLocalized(@"节点库已更新，当前工程需要处理不可用节点。"));return;}
    completed([NSString stringWithUTF8String:refreshed.message.c_str()]);
}
}
