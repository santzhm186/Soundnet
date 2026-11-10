#pragma once
#include "Node.hpp"
#include "../../platform/NodeEditor.h"
#include "ClipEditor.hpp"

#ifdef SNInstanceEditor
#define SNClipCapturePanel SNAUClipCapturePanel
#endif

@interface SNClipCapturePanel : NSView
-(instancetype)initWithContext:(tri::NodeEditorContext)context;
@end

@implementation SNClipCapturePanel {
    tri::NodeEditorContext _context;
    NSButton *_record;
    NSButton *_cancel;
    NSTextField *_status;
    SNMidiClipWindowController *_clipController;
}
-(instancetype)initWithContext:(tri::NodeEditorContext)context {
    self=[super initWithFrame:NSMakeRect(0,0,270,138)];
    if(!self)return nil;
    _context=std::move(context);
    NSButton *edit=[NSButton buttonWithTitle:_context.text(@"编辑 MIDI…") target:self action:@selector(edit:)];
    edit.frame=NSMakeRect(0,104,270,30);[self addSubview:edit];
    _record=[NSButton buttonWithTitle:@"" target:self action:@selector(record:)];
    _record.frame=NSMakeRect(0,68,133,30);
    [self addSubview:_record];
    _cancel=[NSButton buttonWithTitle:_context.text(@"取消录制") target:self action:@selector(cancel:)];
    _cancel.frame=NSMakeRect(139,68,131,30);
    [self addSubview:_cancel];
    _status=[NSTextField wrappingLabelWithString:@""];
    _status.frame=NSMakeRect(0,4,270,60);
    _status.font=[NSFont systemFontOfSize:11];
    _status.textColor=NSColor.secondaryLabelColor;
    [self addSubview:_status];
    [self showRecording:_context.recordingMidi];
    return self;
}
-(void)showRecording:(BOOL)recording {
    _context.recordingMidi=recording;
    _record.enabled=bool(_context.midiCapture);
    _record.title=_context.text(recording?@"完成录制":@"录制 MIDI");
    _cancel.enabled=recording;
    _status.stringValue=_context.text(recording?@"正在录制 MIDI…":
        @"连接 MIDI 输入后点「录制 MIDI」，然后播放上游。完成录制会替换当前片段。");
}
-(void)edit:(id)sender {
    if(_context.isCurrent&&!_context.isCurrent())return;
    _clipController=[[SNMidiClipWindowController alloc]initWithContext:_context owner:self.window];
    [_clipController editClip:nil];
}
-(void)record:(id)sender {
    if(!_context.midiCapture)return;
    if(!_context.recordingMidi) {
        NSDictionary *reply=_context.midiCapture(@"start",tri::nodes::midi_clip::ClockBPM);
        if(reply[@"token"]) [self showRecording:YES];
        else _status.stringValue=_context.text(reply[@"error"] ?: @"无法录制 MIDI。");
        return;
    }
    NSDictionary *reply=_context.midiCapture(@"finish",tri::nodes::midi_clip::ClockBPM);
    [self showRecording:NO];
    if(reply[@"asset"]&&_context.commit) {
        _context.commit(@{@"asset":reply[@"asset"],@"texts":@{}});
    } else _status.stringValue=_context.text(reply[@"error"] ?: @"无法保存 MIDI 录制。");
}
-(void)cancel:(id)sender {
    if(!_context.midiCapture)return;
    _context.midiCapture(@"cancel",tri::nodes::midi_clip::ClockBPM);
    [self showRecording:NO];
}
@end

namespace tri::nodes::midi_clip {
inline NSView* editor(NodeEditorContext context) {
    return [[SNClipCapturePanel alloc] initWithContext:std::move(context)];
}
}
