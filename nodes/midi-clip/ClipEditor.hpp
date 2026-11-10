#pragma once
#import "../../platform/NodeEditor.h"
#import "../../platform/ProjectResources.h"
#import "PianoRoll.hpp"

#ifdef SNInstanceEditor
#define SNMidiClipWindowController SNAUMidiClipWindowController
#endif

@interface SNMidiClipWindowController : NSWindowController <NSWindowDelegate> {
    tri::NodeEditorContext _context;
    __weak NSWindow *_ownerWindow;
}
@property uint64_t auditionGeneration;
@property(strong) NSMutableDictionary<NSNumber *,NSNumber *> *auditionNotes;
@property(strong) NSPanel *clipPanel;
@property(strong) SNNoteRollView *clipRoll;
@property(strong) NSTextField *clipLengthField;
@property(strong) NSDictionary<NSString *,NSTextField *> *clipNoteFields;
@property(strong) NSPopUpButton *clipLanePopup;
@property(strong) NSTextField *clipLaneChannel;
@property(strong) NSTextField *clipLaneNumber;
@property(strong) NSTextField *clipLaneNumberLabel;
@property(strong) NSTextField *clipControllerBeat;
@property(strong) NSTextField *clipControllerValue;
@property(strong) NSArray<NSView *> *clipControllerFields;
-(instancetype)initWithContext:(tri::NodeEditorContext)context owner:(NSWindow *)owner;
-(void)editClip:(id)sender;
@end
@implementation SNMidiClipWindowController
-(instancetype)initWithContext:(tri::NodeEditorContext)context owner:(NSWindow *)owner {
    self=[super initWithWindow:nil];if(self){_context=std::move(context);_ownerWindow=owner;_auditionNotes=[NSMutableDictionary new];}return self;
}
-(BOOL)windowShouldClose:(NSWindow *)sender {[self clipCancel:nil];return NO;}
-(void)message:(NSString *)message {
    NSAlert *alert=[NSAlert new];alert.messageText=message;
    [alert beginSheetModalForWindow:_clipPanel completionHandler:nil];
}
-(void)refreshClipFields {
    using namespace tri::nodes::midi_clip;
    if(!_clipRoll)return;
    const NSInteger index=_clipRoll.selectedIndex;
    NSDictionary *note=index>=0 && index<NSInteger(_clipRoll.notes.count)?_clipRoll.notes[NSUInteger(index)]:nil;
    const auto selected=_clipRoll.selectedNotes;
    for(NSString *key in _clipNoteFields) {
        NSTextField *field=_clipNoteFields[key];
        const bool beats=[key isEqual:@"start"] || [key isEqual:@"length"];
        double value=[note[key] doubleValue]+([key isEqual:@"channel"]?1:0);
        bool mixed=false;
        if(note && selected.count>1)for(NSUInteger i=selected.firstIndex;i!=NSNotFound;i=[selected indexGreaterThanIndex:i])
            if([_clipRoll.notes[i][key] doubleValue]!=[note[key] doubleValue]) {mixed=true;break;}
        field.enabled=note!=nil;
        field.stringValue=note && !mixed?[NSString stringWithFormat:beats?@"%.4f":@"%.0f",value]:@"";
        field.placeholderString=mixed?@"—":@"";
    }
    _clipLengthField.stringValue=[NSString stringWithFormat:@"%.4f",_clipRoll.lengthBeats];
    const auto lane=_clipRoll.lane;
    const NSInteger controller=_clipRoll.selectedController;
    const bool hasController=lane.kind!=Lane::Velocity && controller>=0 && controller<NSInteger(_clipRoll.controllers.count);
    _clipControllerBeat.enabled=_clipControllerValue.enabled=hasController;
    if(hasController) {
        const auto e=controllerEvent(_clipRoll.controllers[NSUInteger(controller)]);
        _clipControllerBeat.stringValue=[NSString stringWithFormat:@"%.4f",e.beat];
        _clipControllerValue.integerValue=lane.value(e);
    } else {
        _clipControllerBeat.stringValue=@"";
        _clipControllerValue.stringValue=@"";
    }
}
-(void)editClip:(id)sender {
    NSDictionary *node=_context.configuration; if(!node||(_context.isCurrent&&!_context.isCurrent()))return;
    tri::MidiClip clip; bool have=false;
    NSDictionary *asset=[node[@"asset"] isKindOfClass:NSDictionary.class] ? node[@"asset"] : nil;
    if(asset)have=tri::projectresources::decodeClip(asset,clip,nullptr);
    double length=have && clip.lengthBeats>0 ? clip.lengthBeats : [asset[@"beats"] doubleValue];
    if(!(length>0))length=4;
    using namespace tri::nodes::midi_clip;
    NSMutableArray<NSMutableDictionary *> *notes=[NSMutableArray array];
    NSMutableArray<NSMutableDictionary *> *controllers=[NSMutableArray array];
    std::map<int,std::pair<double,int>> active;
    for(const auto &e:clip.events) {
        int channel=e.status&15, key=channel*128+e.data1;
        if((e.status&0xf0)==0x90 && e.data2>0) {
            active[key]={e.beat,e.data2};
        } else if((e.status&0xf0)==0x80 || (e.status&0xf0)==0x90) {
            auto it=active.find(key);if(it==active.end())continue;
            double start=it->second.first;
            double duration=std::max(0.0,e.beat-start);
            [notes addObject:[@{@"pitch":@(e.data1),@"start":@(start),@"length":@(duration),
                @"velocity":@(it->second.second),@"channel":@(channel),@"offVelocity":@(e.data2)} mutableCopy]];
            active.erase(it);
        } else [controllers addObject:controllerRow(e)];
    }
    for(const auto &entry:active) {
        int pitch=entry.first%128;
        double start=entry.second.first;
        [notes addObject:[@{@"pitch":@(pitch),@"start":@(start),
            @"length":@(std::max(0.0,length-start)),@"velocity":@(entry.second.second),
            @"channel":@(entry.first/128)} mutableCopy]];
    }
    constexpr double panelWidth=1030,panelHeight=660;
    NSPanel *panel=[[NSPanel alloc]initWithContentRect:NSMakeRect(0,0,panelWidth,panelHeight)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    panel.delegate=self;
    panel.contentMinSize=NSMakeSize(panelWidth,490);
    panel.title=[NSString stringWithFormat:_context.text(@"编辑 MIDI Clip · %@"),asset[@"name"] ?: _context.text(@"新片段")];
    NSView *content=panel.contentView;
    SNNoteRollView *roll=[[SNNoteRollView alloc]initWithFrame:NSMakeRect(0,82,panelWidth,panelHeight-164)];
    roll.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    roll.notes=notes;roll.controllers=controllers;roll.lengthBeats=length;roll.selectedIndex=notes.count?0:-1;
    roll.noteName=^NSString *(int pitch){
        static NSString *const names[]={@"C",@"C♯",@"D",@"E♭",@"E",@"F",@"F♯",@"G",@"A♭",@"A",@"B♭",@"B"};
        return [NSString stringWithFormat:@"%@%d",names[pitch%12],pitch/12-1];
    };
    [content addSubview:roll];[roll fitNotes];

    auto label=[&](NSString *title,double x,double y,double width,bool top) {
        NSTextField *field=[NSTextField labelWithString:_context.text(title)];
        field.font=[NSFont systemFontOfSize:12 weight:NSFontWeightMedium];
        field.frame=NSMakeRect(x,y+3,width,20);
        field.autoresizingMask=top?NSViewMinYMargin:NSViewMaxYMargin;
        [content addSubview:field];return field;
    };
    auto field=[&](NSString *title,double x,double y,double width,SEL action,bool top) {
        NSTextField *input=[[NSTextField alloc]initWithFrame:NSMakeRect(x,y,width,24)];
        input.accessibilityLabel=_context.text(title);
        input.target=self;input.action=action;
        input.cell.sendsActionOnEndEditing=YES;
        input.alignment=NSTextAlignmentRight;
        input.autoresizingMask=top?NSViewMinYMargin:NSViewMaxYMargin;
        [content addSubview:input];return input;
    };
    auto button=[&](NSString *title,double x,double y,double width,SEL action,NSAutoresizingMaskOptions mask) {
        NSButton *control=[NSButton buttonWithTitle:_context.text(title) target:self action:action];
        control.frame=NSMakeRect(x,y,width,28);control.autoresizingMask=mask;
        [content addSubview:control];return control;
    };
    auto popup=[&](NSArray<NSString *> *titles,double x,double y,double width,SEL action,bool top) {
        NSPopUpButton *control=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(x,y,width,26) pullsDown:NO];
        for(NSString *title in titles)[control addItemWithTitle:_context.text(title)];
        control.target=self;control.action=action;
        control.autoresizingMask=top?NSViewMinYMargin:NSViewMaxYMargin;
        [content addSubview:control];return control;
    };

    const double toolbar=panelHeight-35,info=panelHeight-70,laneBar=48,footer=10;
    NSPopUpButton *tool=popup(@[@"智能工具",@"铅笔"],12,toolbar,126,@selector(clipToolChanged:),true);
    tool.accessibilityLabel=_context.text(@"编辑工具");
    label(@"吸附",150,toolbar,48,true);
    NSPopUpButton *gridPopup=popup(@[@"关闭",@"1/4",@"1/8",@"1/16",@"1/32",@"1/16T"],
        198,toolbar,86,@selector(clipGridChanged:),true);
    gridPopup.accessibilityLabel=_context.text(@"吸附");[gridPopup selectItemAtIndex:3];
    button(@"−",297,toolbar,32,@selector(clipZoomOut:),NSViewMinYMargin);
    button(@"+",331,toolbar,32,@selector(clipZoomIn:),NSViewMinYMargin);
    button(@"适配",366,toolbar,62,@selector(clipFit:),NSViewMinYMargin);
    button(@"量化",444,toolbar,91,@selector(clipQuantize:),NSViewMinYMargin);
    button(@"试听音符",548,toolbar,84,@selector(clipPreview:),NSViewMinYMargin);
    button(@"试听全部音符",635,toolbar,112,@selector(clipAudition:),NSViewMinYMargin);
    NSButton *stop=button(@"停止",749,toolbar,82,@selector(stopGraph:),NSViewMinYMargin);
    stop.keyEquivalent=@".";stop.keyEquivalentModifierMask=NSEventModifierFlagCommand;

    NSArray *columns=@[@[@"起点（拍）",@"start",@94,@74],@[@"长度（拍）",@"length",@97,@74],
        @[@"音高",@"pitch",@42,@48],@[@"力度",@"velocity",@57,@48],
        @[@"松键力度",@"offVelocity",@90,@48],@[@"通道",@"channel",@60,@46]];
    NSMutableDictionary *noteFields=[NSMutableDictionary new];
    double x=14;
    for(NSArray *column in columns) {
        NSString *title=column[0],*key=column[1];
        const double labelWidth=[column[2] doubleValue],fieldWidth=[column[3] doubleValue];
        label(title,x,info,labelWidth,true);
        noteFields[key]=field(title,x+labelWidth,info,fieldWidth,@selector(clipNoteFieldChanged:),true);
        x+=labelWidth+fieldWidth+10;
    }
    label(@"片段长度",x,info,86,true);
    NSTextField *clipField=field(@"片段长度",x+86,info,71,@selector(clipLengthChanged:),true);

    self.clipLanePopup=popup(@[@"力度",@"MIDI CC",@"弯音",@"通道触后",@"复音触后",@"音色切换"],
        12,laneBar,158,@selector(clipLaneChanged:),false);
    _clipLanePopup.accessibilityLabel=_context.text(@"控制器轨道");
    label(@"通道",182,laneBar,60,false);
    self.clipLaneChannel=field(@"通道",242,laneBar,43,@selector(clipLaneChanged:),false);
    _clipLaneChannel.integerValue=1;
    self.clipLaneNumberLabel=label(@"编号",298,laneBar,83,false);
    self.clipLaneNumber=field(@"编号",381,laneBar,47,@selector(clipLaneChanged:),false);
    _clipLaneNumber.integerValue=1;
    NSTextField *beatLabel=label(@"位置（拍）",458,laneBar,88,false);
    self.clipControllerBeat=field(@"位置（拍）",546,laneBar,78,@selector(clipControllerChanged:),false);
    NSTextField *valueLabel=label(@"数值",642,laneBar,48,false);
    self.clipControllerValue=field(@"数值",690,laneBar,69,@selector(clipControllerChanged:),false);
    self.clipControllerFields=@[beatLabel,_clipControllerBeat,valueLabel,_clipControllerValue];
    button(@"删除所选",774,laneBar,111,@selector(clipDelete:),NSViewMaxYMargin);

    NSTextField *hint=label(@"双击添加 · Shift 多选 · 滚动音高 · Shift 滚动时间 · ⌘ 滚动缩放",14,footer,780,false);
    hint.font=[NSFont systemFontOfSize:11];hint.textColor=NSColor.secondaryLabelColor;
    NSButton *cancel=button(@"取消",836,footer,82,@selector(clipCancel:),NSViewMinXMargin|NSViewMaxYMargin);
    cancel.keyEquivalent=@"\e";
    NSButton *apply=button(@"应用",924,footer,94,@selector(clipApply:),NSViewMinXMargin|NSViewMaxYMargin);
    apply.keyEquivalent=@"\r";
    __weak SNMidiClipWindowController *weakSelf=self;
    roll.selectionChanged=^{ [weakSelf refreshClipFields]; };
    self.clipPanel=panel;self.clipRoll=roll;self.window=panel;
    self.clipLengthField=clipField;self.clipNoteFields=noteFields;
    [self clipLaneChanged:nil];
    [_ownerWindow beginSheet:panel completionHandler:^(NSModalResponse response){[self cancelAudition];}];
}
-(void)sendNote:(int)pitch channel:(int)channel velocity:(int)velocity on:(BOOL)on {
    if(!_context.sendMidi)return;
    NSNumber *key=@(channel*128+pitch);NSInteger count=[_auditionNotes[key] integerValue];
    if(on){_auditionNotes[key]=@(count+1);_context.sendMidi(uint8_t(0x90|channel),uint8_t(pitch),uint8_t(velocity));}
    else if(count>0){_context.sendMidi(uint8_t(0x80|channel),uint8_t(pitch),0);if(count>1)_auditionNotes[key]=@(count-1);else [_auditionNotes removeObjectForKey:key];}
}
-(void)cancelAudition {
    ++_auditionGeneration;
    if(_context.sendMidi)for(NSNumber *key in _auditionNotes.allKeys) {
        const int channel=key.intValue/128,pitch=key.intValue%128;
        for(NSInteger i=0;i<[_auditionNotes[key] integerValue];++i)_context.sendMidi(uint8_t(0x80|channel),uint8_t(pitch),0);
    }
    [_auditionNotes removeAllObjects];
}
-(void)stopGraph:(id)sender {
    [self cancelAudition];if(_context.stopPlayback)_context.stopPlayback();
}
-(BOOL)canAudition {
    return !_context.isCurrent||_context.isCurrent();
}
-(void)scheduleNote:(NSDictionary *)note secondsPerBeat:(double)secondsPerBeat offset:(double)offset generation:(uint64_t)generation {
    const int pitch=std::clamp([note[@"pitch"] intValue],0,127),channel=std::clamp([note[@"channel"] intValue],0,15);
    const int velocity=std::clamp([note[@"velocity"] intValue],1,127);
    const double start=std::max(0.0,([note[@"start"] doubleValue]-offset)*secondsPerBeat);
    const double length=std::max(.01,[note[@"length"] doubleValue]*secondsPerBeat);
    __weak SNMidiClipWindowController *weakSelf=self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,int64_t(start*NSEC_PER_SEC)),dispatch_get_main_queue(),^{
        SNMidiClipWindowController *strong=weakSelf;
        if(!strong||strong.auditionGeneration!=generation||![strong canAudition])return;
        [strong sendNote:pitch channel:channel velocity:velocity on:YES];
    });
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,int64_t((start+length)*NSEC_PER_SEC)),dispatch_get_main_queue(),^{
        SNMidiClipWindowController *strong=weakSelf;
        if(!strong||strong.auditionGeneration!=generation)return;
        [strong sendNote:pitch channel:channel velocity:0 on:NO];
    });
}
-(void)clipPreview:(id)sender {
    const NSInteger index=_clipRoll.selectedIndex;
    if(index<0||index>=NSInteger(_clipRoll.notes.count)||![self canAudition])return;
    [self cancelAudition];if(_context.resumePlayback)_context.resumePlayback();
    NSMutableDictionary *note=[_clipRoll.notes[NSUInteger(index)] mutableCopy];note[@"length"]=@.25;
    [self scheduleNote:note secondsPerBeat:1 offset:[note[@"start"] doubleValue] generation:_auditionGeneration];
}
-(void)clipAudition:(id)sender {
    if(!_clipRoll.notes.count||![self canAudition])return;
    [self cancelAudition];if(_context.resumePlayback)_context.resumePlayback();
    double bpm=[_context.configuration[@"parameters"][@"clock.bpm"] doubleValue];
    if(!std::isfinite(bpm)||bpm<=1)bpm=112;
    for(NSDictionary *note in _clipRoll.notes)[self scheduleNote:note secondsPerBeat:60.0/bpm offset:0 generation:_auditionGeneration];
}
-(void)clipToolChanged:(NSPopUpButton *)sender {_clipRoll.pencil=sender.indexOfSelectedItem==1;}
-(void)clipZoomIn:(id)sender {(void)sender;[_clipRoll zoom:1.5];}
-(void)clipZoomOut:(id)sender {(void)sender;[_clipRoll zoom:1/1.5];}
-(void)clipFit:(id)sender {(void)sender;[_clipRoll fitNotes];}
-(void)clipGridChanged:(NSPopUpButton *)sender {
    static constexpr double divisions[]={0,1,.5,.25,.125,1.0/6};
    const NSInteger index=sender.indexOfSelectedItem;
    if(index<0 || index>=NSInteger(std::size(divisions)))return;
    _clipRoll.snapEnabled=index!=0;
    if(index)_clipRoll.grid=divisions[index];
}
-(void)clipLaneChanged:(id)sender {
    using namespace tri::nodes::midi_clip;
    auto lane=_clipRoll.lane;
    lane.kind=Lane(std::clamp(_clipLanePopup.indexOfSelectedItem,NSInteger(0),NSInteger(5)));
    lane.channel=std::clamp(int(_clipLaneChannel.intValue),1,16)-1;
    lane.number=std::clamp(int(_clipLaneNumber.intValue),0,127);
    _clipLaneChannel.integerValue=lane.channel+1;
    _clipLaneNumber.integerValue=lane.number;
    _clipLaneNumber.hidden=_clipLaneNumberLabel.hidden=!lane.usesNumber();
    for(NSView *view in _clipControllerFields)view.hidden=lane.kind==Lane::Velocity;
    if(lane.kind==Lane::ControlChange) {
        NSString *name=nil;
        switch(lane.number) {
            case 1:name=@"调制";break; case 7:name=@"音量";break; case 10:name=@"声像";break;
            case 11:name=@"表情";break; case 64:name=@"延音踏板";break;
            case 66:name=@"持音踏板";break; case 67:name=@"柔音踏板";break;
        }
        _clipLaneNumberLabel.stringValue=name?_context.text(name):@"CC";
    } else _clipLaneNumberLabel.stringValue=_context.text(@"音高");
    _clipRoll.lane=lane;
    (void)sender;
}
-(void)clipControllerChanged:(id)sender {
    (void)sender;
    if(!_clipControllerBeat.stringValue.length || !_clipControllerValue.stringValue.length)return;
    [_clipRoll updateSelectedControllerAt:_clipControllerBeat.doubleValue value:_clipControllerValue.intValue];
}
-(void)clipQuantize:(id)sender {
    (void)sender;
    using namespace tri::nodes::midi_clip;
    const double grid=_clipRoll.grid;
    const NSIndexSet *selected=_clipRoll.selectedNotes;
    for(NSUInteger i=0;i<_clipRoll.notes.count;++i) {
        if(selected.count && ![selected containsIndex:i])continue;
        NSMutableDictionary *note=_clipRoll.notes[i];
        const double start=[note[@"start"] doubleValue],length=[note[@"length"] doubleValue];
        note[@"start"]=@(std::clamp(snap(start,grid),0.0,std::max(0.0,_clipRoll.lengthBeats-length)));
    }
    [_clipRoll setNeedsDisplay:YES];[self refreshClipFields];
}
-(void)clipApply:(id)sender {
    [self cancelAudition];
    [_clipPanel makeFirstResponder:_clipRoll];
    NSDictionary *node=_context.configuration; if(!node||!_clipRoll||(_context.isCurrent&&!_context.isCurrent())){[self message:_context.text(@"节点已不存在")];return;}
    if(!_clipRoll.notes.count && !_clipRoll.controllers.count){[self message:_context.text(@"请先添加音符或控制器事件")];return;}
    tri::MidiClip clip;clip.lengthBeats=_clipRoll.lengthBeats;
    NSArray<NSDictionary *> *sorted=[_clipRoll.notes sortedArrayUsingComparator:^NSComparisonResult(NSDictionary *a,NSDictionary *b){
        return [a[@"start"] doubleValue]<[b[@"start"] doubleValue]?NSOrderedAscending:
               [a[@"start"] doubleValue]>[b[@"start"] doubleValue]?NSOrderedDescending:NSOrderedSame;
    }];
    for(NSDictionary *note in sorted) {
        constexpr double ticksPerBeat=tri::midiClipTicksPerBeat;
        const double originalStart=[note[@"start"] doubleValue];
        const double startTick=std::round(originalStart*ticksPerBeat);
        const double endTick=std::max(startTick+1,
            std::round((originalStart+[note[@"length"] doubleValue])*ticksPerBeat));
        const double start=startTick/ticksPerBeat,end=endTick/ticksPerBeat;
        int pitch=std::max(0,std::min(127,[note[@"pitch"] intValue]));
        int velocity=std::max(1,std::min(127,[note[@"velocity"] intValue]));
        const int channel=std::clamp([note[@"channel"] intValue],0,15);
        const int offVelocity=std::clamp([note[@"offVelocity"] intValue],0,127);
        clip.events.push_back({start,uint8_t(0x90|channel),uint8_t(pitch),uint8_t(velocity)});
        clip.events.push_back({end,uint8_t(0x80|channel),uint8_t(pitch),uint8_t(offVelocity)});
        clip.lengthBeats=std::max(clip.lengthBeats,end);
    }
    for(NSDictionary *row in _clipRoll.controllers) {
        const auto event=tri::nodes::midi_clip::controllerEvent(row);
        clip.events.push_back(event);
        clip.lengthBeats=std::max(clip.lengthBeats,event.beat);
    }
    clip.lengthBeats=std::round(clip.lengthBeats*tri::midiClipTicksPerBeat)/tri::midiClipTicksPerBeat;
    NSString *why=nil;
    NSDictionary *asset=tri::projectresources::encodeClip(node[@"asset"][@"name"] ?: @"Clip",clip,&why);
    if(!asset){[self message:why ?: _context.text(@"无法编码 MIDI Clip")];return;}
    if(!_context.commit)return;
    _context.commit(@{@"asset":asset});
    [_ownerWindow endSheet:_clipPanel];
    self.clipPanel=nil;self.clipRoll=nil;self.clipLengthField=nil;self.clipNoteFields=nil;
}
-(void)clipCancel:(id)sender {
    [self cancelAudition];
    [_ownerWindow endSheet:_clipPanel];
    self.clipPanel=nil;self.clipRoll=nil;self.clipLengthField=nil;self.clipNoteFields=nil;
}
-(void)clipDelete:(id)sender { [_clipRoll deleteSelected]; [self refreshClipFields]; }
-(void)clipLengthChanged:(id)sender {
    double minimum=tri::nodes::midi_clip::editTick;
    for(NSDictionary *note in _clipRoll.notes)
        minimum=std::max(minimum,[note[@"start"] doubleValue]+[note[@"length"] doubleValue]);
    for(NSDictionary *row in _clipRoll.controllers)minimum=std::max(minimum,[row[@"beat"] doubleValue]);
    _clipRoll.lengthBeats=std::max(minimum,_clipLengthField.doubleValue);
    [self refreshClipFields];
}
-(void)clipNoteFieldChanged:(NSTextField *)sender {
    if(!sender.stringValue.length)return;
    for(NSString *key in _clipNoteFields)if(_clipNoteFields[key]==sender) {
        const double value=sender.doubleValue-([key isEqual:@"channel"]?1:0);
        [_clipRoll updateSelectedNote:key value:value];
        break;
    }
}
@end
