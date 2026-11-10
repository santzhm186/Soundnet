#pragma once
#include "../../platform/NodeEditor.h"
#include "Parameters.hpp"
#ifdef SNInstanceEditor
#define SNVoiceArrangerPanel SNAUVoiceArrangerPanel
#endif
@interface SNVoiceArrangerPanel : NSView
-(instancetype)initWithContext:(tri::NodeEditorContext)context;
@end
@implementation SNVoiceArrangerPanel {
    tri::NodeEditorContext _context;
    NSMutableDictionary* _configuration;
    NSMutableArray<NSTextField*>* _bounds;
    NSTextField* _status;
}
-(BOOL)isFlipped{return YES;}
-(instancetype)initWithContext:(tri::NodeEditorContext)context {
    self=[super initWithFrame:NSMakeRect(0,0,270,490)];if(!self)return nil;
    _context=std::move(context);_configuration=[_context.configuration mutableCopy]?:[NSMutableDictionary new];_bounds=[NSMutableArray new];
    NSTextField* note=[NSTextField wrappingLabelWithString:_context.text(@"音域用 MIDI 音号表示（60 = C4）。选出的音按八度移入各声部音域。")];note.frame=NSMakeRect(0,0,270,46);note.font=[NSFont systemFontOfSize:12];[self addSubview:note];
    NSArray* voices=@[@"high",@"middle",@"low"];
    NSArray* names=@[@"高声部",@"中声部",@"低声部"];
    const int defaults[3][3]={{72,95,0},{55,71,1},{36,54,2}};
    NSDictionary* parameters=_configuration[@"parameters"];
    for(int v=0;v<3;++v) {
        const CGFloat y=52+v*134;
        NSTextField* title=[NSTextField labelWithString:_context.text(names[v])];title.font=[NSFont boldSystemFontOfSize:13];title.frame=NSMakeRect(0,y,270,20);[self addSubview:title];
        for(int b=0;b<2;++b) {
            NSString* caption=_context.text(b?@"最高音":@"最低音");CGFloat x=b?141:0;
            NSTextField* label=[NSTextField labelWithString:caption];label.frame=NSMakeRect(x,y+24,129,18);[self addSubview:label];
            NSTextField* field=[[NSTextField alloc]initWithFrame:NSMakeRect(x,y+44,129,25)];field.tag=v*2+b;
            NSString* key=[NSString stringWithFormat:@"%@.%@",voices[v],b?@"maximum":@"minimum"];
            field.integerValue=parameters[key]?[parameters[key] integerValue]:defaults[v][b];field.target=self;field.action=@selector(range:);
            field.accessibilityLabel=[_context.text(names[v]) stringByAppendingString:caption];[self addSubview:field];[_bounds addObject:field];
        }
        NSPopUpButton* popup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(0,y+76,270,27) pullsDown:NO];
        for(NSString* item in @[@"最高音",@"中间音",@"最低音",@"柱式和弦（全部音）"]) [popup addItemWithTitle:_context.text(item)];
        NSString* key=[voices[v] stringByAppendingString:@".texture"];
        [popup selectItemAtIndex:parameters[key]?[parameters[key] integerValue]:defaults[v][2]];
        popup.tag=v;popup.target=self;popup.action=@selector(texture:);popup.accessibilityLabel=[_context.text(names[v]) stringByAppendingString:_context.text(@"织体")];[self addSubview:popup];
    }
    _status=[NSTextField wrappingLabelWithString:@""];_status.frame=NSMakeRect(0,458,270,32);_status.font=[NSFont systemFontOfSize:10];_status.textColor=NSColor.systemOrangeColor;[self addSubview:_status];return self;
}
-(BOOL)current{return _context.commit&&(!_context.isCurrent||_context.isCurrent());}
-(void)publish:(NSString*)key value:(NSInteger)value {
    if(![self current])return;
    NSMutableDictionary* parameters=[_configuration[@"parameters"] isKindOfClass:NSDictionary.class]?[_configuration[@"parameters"] mutableCopy]:[NSMutableDictionary new];
    parameters[key]=@(value);_configuration[@"parameters"]=parameters;_context.commit(_configuration);
}
-(void)range:(NSTextField*)field {
    if(![self current])return;
    const NSInteger voice=field.tag/2;NSInteger value=0;
    NSScanner* scan=[NSScanner scannerWithString:field.stringValue];BOOL valid=[scan scanInteger:&value]&&scan.isAtEnd&&value>=0&&value<=127;
    const NSInteger low=field.tag%2?_bounds[voice*2].integerValue:value,high=field.tag%2?value:_bounds[voice*2+1].integerValue;
    if(!valid||low>high){_status.stringValue=_context.text(@"音域需为 0–127 的整数，最低音不能高于最高音。");return;}
    NSString* key=[NSString stringWithFormat:@"%@.%@",@[@"high",@"middle",@"low"][voice],field.tag%2?@"maximum":@"minimum"];
    [self publish:key value:value];
}
-(void)texture:(NSPopUpButton*)sender {
    NSString* key=[@[@"high",@"middle",@"low"][sender.tag] stringByAppendingString:@".texture"];
    [self publish:key value:sender.indexOfSelectedItem];
}
@end
namespace tri::nodes::voice_arranger {
inline NSView* editor(NodeEditorContext context){return [[SNVoiceArrangerPanel alloc]initWithContext:std::move(context)];}
}
