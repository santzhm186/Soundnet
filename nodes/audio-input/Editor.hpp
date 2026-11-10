#pragma once
#include "../../platform/NodeEditor.h"
#ifdef SNInstanceEditor
#define SNAudioInputPanel SNAUAudioInputPanel
#endif
@interface SNAudioInputPanel : NSView
-(instancetype)initWithContext:(tri::NodeEditorContext)context;
@end
@implementation SNAudioInputPanel {
    tri::NodeEditorContext _context;
}
-(instancetype)initWithContext:(tri::NodeEditorContext)context {
    self=[super initWithFrame:NSMakeRect(0,0,270,48)];if(!self)return nil;_context=std::move(context);
    if(_context.setAudioInputEnabled) {
        NSButton *toggle=[NSButton checkboxWithTitle:_context.text(@"启用音频输入") target:self action:@selector(toggle:)];
        toggle.frame=NSMakeRect(0,10,270,28);toggle.state=_context.audioInputEnabled&&_context.audioInputEnabled()?NSControlStateValueOn:NSControlStateValueOff;
        [self addSubview:toggle];
    } else {
        NSTextField *label=[NSTextField wrappingLabelWithString:_context.text(@"在 AU 中接收宿主的侧链输入。")];
        label.frame=NSMakeRect(0,0,270,44);label.font=[NSFont systemFontOfSize:12];[self addSubview:label];
    }
    return self;
}
-(void)toggle:(NSButton *)sender {
    if(_context.isCurrent&&!_context.isCurrent())return;
    if(_context.setAudioInputEnabled)_context.setAudioInputEnabled(sender.state==NSControlStateValueOn);
}
@end
namespace tri::nodes::audio_input {
inline NSView* editor(NodeEditorContext context){return [[SNAudioInputPanel alloc]initWithContext:std::move(context)];}
}
