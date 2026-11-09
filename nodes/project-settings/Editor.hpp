#pragma once
#include "../../platform/NodeEditor.h"
#import <objc/runtime.h>
// The AU editor may coexist with the standalone editor in one process.
#ifdef SNInstanceEditor
#define SNProjectDeviceAction SNAUProjectDeviceAction
#endif
@interface SNProjectDeviceAction : NSObject
@property(nonatomic,copy) NSDictionary* configuration;
@property(nonatomic,copy) void (^commit)(NSDictionary*);
-(void)changed:(NSPopUpButton*)sender;
@end
@implementation SNProjectDeviceAction
-(void)changed:(NSPopUpButton*)sender {
    if(!_commit)return;
    NSString* uid=sender.selectedItem.representedObject;
    if(![uid isKindOfClass:NSString.class])return;
    NSMutableDictionary* replacement=[_configuration mutableCopy];
    NSMutableDictionary* texts=[replacement[@"texts"] isKindOfClass:NSDictionary.class]
        ? [replacement[@"texts"] mutableCopy] : [NSMutableDictionary new];
    texts[sender.tag==0?@"project.inputDevice":@"project.outputDevice"]=uid;
    replacement[@"texts"]=texts;
    _commit(replacement);
}
-(void)chooseFolder:(id)sender {
    if(!_commit)return;
    NSOpenPanel* panel=[NSOpenPanel openPanel];
    panel.canChooseDirectories=YES;panel.canChooseFiles=NO;panel.canCreateDirectories=YES;
    NSString* current=_configuration[@"texts"][@"project.folder"];
    if([current isKindOfClass:NSString.class]&&current.length)
        panel.directoryURL=[NSURL fileURLWithPath:current.stringByExpandingTildeInPath];
    [panel beginWithCompletionHandler:^(NSModalResponse result) {
        if(result!=NSModalResponseOK||!panel.URL.path.length)return;
        NSMutableDictionary* replacement=[_configuration mutableCopy];
        NSMutableDictionary* texts=[replacement[@"texts"] isKindOfClass:NSDictionary.class]
            ? [replacement[@"texts"] mutableCopy] : [NSMutableDictionary new];
        texts[@"project.folder"]=panel.URL.path;
        replacement[@"texts"]=texts;
        _commit(replacement);
    }];
}
@end
namespace tri::nodes::project_settings {
inline NSView* editor(NodeEditorContext context) {
    NSView* view=[[NSView alloc] initWithFrame:NSMakeRect(0,0,270,268)];
    const bool managed=[context.environment[@"hostManaged"] boolValue];
    SNProjectDeviceAction* action=[SNProjectDeviceAction new];
    action.configuration=context.configuration ?: @{};
    action.commit=context.commit;
    static const char actionKey=0;
    objc_setAssociatedObject(view,&actionKey,action,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    for(int i=0;i<2;++i) {
        const CGFloat top=244-i*51;
        NSTextField* label=[NSTextField labelWithString:context.text(i==0?@"输入设备":@"输出设备")];
        label.font=[NSFont systemFontOfSize:12];label.frame=NSMakeRect(0,top,270,18);
        [view addSubview:label];
        NSPopUpButton* popup=[[NSPopUpButton alloc] initWithFrame:NSMakeRect(0,top-28,270,26) pullsDown:NO];
        popup.tag=i;popup.target=action;popup.action=@selector(changed:);
        popup.accessibilityLabel=label.stringValue;
        [popup addItemWithTitle:context.text(@"跟随系统默认")];
        popup.lastItem.representedObject=@"system";
        NSArray* devices=[context.environment[@"devices"] isKindOfClass:NSArray.class]?context.environment[@"devices"]:@[];
        for(NSDictionary* device in devices) {
            if(![device[i==0?@"input":@"output"] boolValue])continue;
            NSString* uid=device[@"uid"],*name=device[@"name"];
            if(![uid isKindOfClass:NSString.class]||!uid.length)continue;
            [popup addItemWithTitle:[name isKindOfClass:NSString.class]&&name.length?name:uid];
            popup.lastItem.representedObject=uid;
        }
        NSString* selected=context.configuration[@"texts"][i==0?@"project.inputDevice":@"project.outputDevice"];
        if(![selected isKindOfClass:NSString.class]||!selected.length)selected=@"system";
        BOOL found=NO;
        for(NSMenuItem* item in popup.itemArray)if([item.representedObject isEqual:selected]) {
            [popup selectItem:item];found=YES;break;
        }
        if(!found) {
            [popup addItemWithTitle:[NSString stringWithFormat:@"%@ (%@)",context.text(@"设备不可用"),selected]];
            popup.lastItem.representedObject=selected;[popup selectItem:popup.lastItem];
        }
        popup.enabled=!managed&&context.commit!=nil;
        [view addSubview:popup];
    }
    // Project folder: where generation nodes archive their takes. The path
    // itself stays an ordinary (visible) text parameter; this picker only
    // writes it through the normal commit transaction.
    {
        const CGFloat top=128;
        NSTextField* label=[NSTextField labelWithString:context.text(@"工程文件夹")];
        label.font=[NSFont systemFontOfSize:12];label.frame=NSMakeRect(0,top,270,18);
        [view addSubview:label];
        NSButton* choose=[NSButton buttonWithTitle:context.text(@"选择文件夹…")
                                             target:action action:@selector(chooseFolder:)];
        choose.frame=NSMakeRect(0,top-30,270,28);
        choose.enabled=context.commit!=nil;
        [view addSubview:choose];
        NSString* folder=context.configuration[@"texts"][@"project.folder"];
        const bool hasFolder=[folder isKindOfClass:NSString.class]&&folder.length>0;
        NSString* note=[NSString stringWithFormat:@"%@ %@",
            hasFolder?folder:@"",
            context.text(hasFolder?@"生成素材将归档到该文件夹的「AI 生成」子文件夹。"
                                 :@"未设定（生成结果仅保留在节点内）")];
        NSTextField* status=[NSTextField wrappingLabelWithString:note];
        status.frame=NSMakeRect(0,top-58,270,44);status.font=[NSFont systemFontOfSize:10];
        status.textColor=NSColor.secondaryLabelColor;
        status.lineBreakMode=NSLineBreakByTruncatingMiddle;
        [view addSubview:status];
    }
    const double actual=[context.environment[@"actualRate"] doubleValue];
    const double device=[context.environment[@"deviceRate"] doubleValue];
    NSString* status=managed?context.text(@"音频设备与采样率由宿主管理"):
        [NSString stringWithFormat:@"%@ %.0f Hz · %@ %.0f Hz",context.text(@"处理"),actual,context.text(@"设备"),device];
    NSTextField* statusField=[NSTextField wrappingLabelWithString:status];
    statusField.frame=NSMakeRect(0,8,270,35);statusField.font=[NSFont systemFontOfSize:11];
    statusField.textColor=NSColor.secondaryLabelColor;[view addSubview:statusField];
    NSString* error=context.environment[@"error"];
    if([error isKindOfClass:NSString.class]&&error.length) {
        NSTextField* errorField=[NSTextField wrappingLabelWithString:error];
        errorField.frame=NSMakeRect(0,0,270,30);errorField.font=[NSFont systemFontOfSize:10];
        errorField.textColor=NSColor.systemOrangeColor;[view addSubview:errorField];
    }
    return view;
}
}
