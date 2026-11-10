#pragma once
#include "../../platform/NodeEditor.h"
#include "../../platform/music/Harmony.hpp"
#ifdef SNInstanceEditor
#define SNChordPanel SNAUChordPanel
#endif
@interface SNChordPanel : NSView
-(instancetype)initWithContext:(tri::NodeEditorContext)context;
@end
@implementation SNChordPanel {
    tri::NodeEditorContext _context;NSTextField* _field;NSTextField* _status;
}
-(BOOL)isFlipped{return YES;}
-(instancetype)initWithContext:(tri::NodeEditorContext)context {
    self=[super initWithFrame:NSMakeRect(0,0,270,116)];if(!self)return nil;_context=std::move(context);
    NSTextField* title=[NSTextField labelWithString:_context.text(@"和弦 / 走向，可手动输入")];title.frame=NSMakeRect(0,0,270,20);[self addSubview:title];
    _field=[[NSTextField alloc]initWithFrame:NSMakeRect(0,24,270,26)];_field.placeholderString=@"Cmaj7";_field.target=self;_field.action=@selector(apply:);_field.accessibilityLabel=_context.text(@"和弦输入");[self addSubview:_field];
    NSButton* preset=[NSButton buttonWithTitle:_context.text(@"预制 Cmaj7 / 爵士走向") target:self action:@selector(preset:)];preset.frame=NSMakeRect(0,54,270,28);[self addSubview:preset];
    _status=[NSTextField wrappingLabelWithString:@""];_status.frame=NSMakeRect(0,86,270,30);_status.font=[NSFont systemFontOfSize:10];[self addSubview:_status];return self;
}
-(void)preset:(id)sender {_field.stringValue=@"Cmaj7";[self apply:sender];}
-(void)apply:(id)sender {
    if(!_context.commit||(_context.isCurrent&&!_context.isCurrent()))return;
    auto parsed=tri::parseProgression(_field.stringValue.UTF8String?:"");
    if(!parsed){_status.stringValue=_context.text(@"请输入和弦名称，如 Cmaj7 | Am7 | Dm7 | G7。");return;}
    NSMutableDictionary* replacement=[_context.configuration mutableCopy]?:[NSMutableDictionary new];
    NSMutableDictionary* parameters=[replacement[@"parameters"] isKindOfClass:NSDictionary.class]?[replacement[@"parameters"] mutableCopy]:[NSMutableDictionary new];
    parameters[@"chord.root"]=@((*parsed)[0].root);parameters[@"chord.quality"]=@((*parsed)[0].quality);
    replacement[@"parameters"]=parameters;_context.commit(replacement);
}
@end
namespace tri::nodes::chord {
inline NSView* editor(NodeEditorContext context){return [[SNChordPanel alloc]initWithContext:std::move(context)];}
}
