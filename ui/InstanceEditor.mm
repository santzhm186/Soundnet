#import "TriEditor.h"
#import "WorkflowFile.hpp"
#import "../src/WorkflowAssets.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace {
NSArray *catalog() {
    return @[
        @[@1,@"本地 MIDI",@"输入"],@[@15,@"DAW MIDI",@"输入"],@[@16,@"MIDI Clip",@"输入"],
        @[@6,@"硬件音频输入",@"输入"],@[@22,@"DAW 音频输入",@"输入"],
        @[@11,@"和弦输入",@"输入"],@[@12,@"和声走向",@"输入"],
        @[@13,@"节奏速度",@"输入"],@[@21,@"DAW 时钟",@"输入"],
        @[@3,@"振荡器",@"处理"],@[@2,@"ARP",@"处理"],@[@(tri::ArrangerNode),@"三声部编排器",@"处理"],@[@14,@"力度自动化",@"处理"],
        @[@17,@"音符模型",@"处理"],@[@7,@"混音器",@"处理"],@[@8,@"EQ",@"处理"],@[@9,@"Delay",@"处理"],
        @[@23,@"开始触发器",@"逻辑"],@[@24,@"停止触发器",@"逻辑"],@[@25,@"等待",@"逻辑"],@[@26,@"次数条件",@"逻辑"],
        @[@19,@"硬件音频输出",@"输出"],@[@20,@"DAW 音频输出",@"输出"],@[@18,@"MIDI 输出",@"输出"]];
}
NSString *title(int type) {
    if(type==10)return @"音频输出";
    for(NSArray *row in catalog())if([row[0] intValue]==type)return row[1];
    return @"节点";
}
NSArray<NSString *> *voiceNames() { return @[@"高声部", @"中声部", @"低声部"]; }
struct VoiceControls { int lower,upper,texture; };
constexpr VoiceControls voiceControls[]={
    {tri::ArrHighMin,tri::ArrHighMax,tri::ArrHighTexture},
    {tri::ArrMidMin,tri::ArrMidMax,tri::ArrMidTexture},
    {tri::ArrLowMin,tri::ArrLowMax,tri::ArrLowTexture}
};
NSNumber *voiceValue(NSDictionary *node,int pid) {
    return node[@"parameters"][[@(pid) stringValue]] ?: @(tri::graphParamInfo(pid).def);
}
NSString *pitchName(int note) {
    static NSString *const names[]={@"C",@"C♯",@"D",@"E♭",@"E",@"F",@"F♯",@"G",@"A♭",@"A",@"B♭",@"B"};
    return [NSString stringWithFormat:@"%@%d",names[note%12],note/12-1];
}
NSString *outputName(int type,int port) {
    if(type==tri::ArrangerNode)return voiceNames()[port];
    if(type==tri::ClipNode && port==1)return @"循环完成";
    return @"输出";
}
NSColor *signalColor(tri::Signal signal) {
    if(signal==tri::Signal::Notes)return [NSColor colorWithCalibratedRed:.9 green:.7 blue:.4 alpha:1];
    if(signal==tri::Signal::Audio)return [NSColor colorWithCalibratedRed:.4 green:.75 blue:.65 alpha:1];
    if(signal==tri::Signal::Trigger)return [NSColor colorWithCalibratedRed:1 green:.45 blue:.4 alpha:1];
    return [NSColor colorWithCalibratedRed:.65 green:.6 blue:.85 alpha:1];
}
NSMutableDictionary *copyJSON(id value) {
    if(!value)return nil;
    NSData *data=[NSJSONSerialization dataWithJSONObject:value options:0 error:nil];
    return [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:nil];
}
std::vector<int> controls(int type) {
    using namespace tri;
    switch(type) {
        case Osc1Node:return {Wave1,Level1,Tune1,Attack,Decay,Sustain,Release};
        case ArpNode:return {ArpOn,tri::Pattern,ArpRepeat,Gate};
        case ChordNode:return {ChordRun,ChordRoot,ChordQuality};
        case ProgressNode:return {ProgressRun,ProgressLength,ProgressBars};
        case ClipNode:return {ClipRun,ClipLoop};
        case ClockNode:return {ClockBPM};
        case VelocityNode:return {VelocityMin,VelocityMax,VelocityRate,VelocityShape};
        case EQNode:return {EQLow,EQMid,EQHigh};
        case DelayNode:return {DelayTime,DelayFeedback,DelayMix};
        case OutputNode:case HardwareOutputNode:case DawOutputNode:return {Master};
        case WaitNode:return {WaitSeconds};
        case CountNode:return {TriggerCount};
        case ArrangerNode:return {ArrHighMin,ArrHighMax,ArrHighTexture,ArrMidMin,ArrMidMax,ArrMidTexture,ArrLowMin,ArrLowMax,ArrLowTexture};
        default:return {};
    }
}
NSString *parameterName(int p) {
    switch(p) {
        case tri::TriggerCount:return @"收到 X 次后触发";
        case tri::WaitSeconds:return @"等待秒数";
        case tri::ClockBPM:return @"BPM";
        case tri::ClipRun:return @"播放  0 / 1";
        case tri::ClipLoop:return @"循环  0 / 1";
        default:return @(tri::params[p].name);
    }
}
}

@class SNInstanceEditor;
@interface SNInstanceCanvas : NSView
@property(weak) SNInstanceEditor *editor;
@end

@interface SNInstanceEditor : NSView <NSTextFieldDelegate>
@property(copy) TRIRead read;
@property(copy) TRIWrite write;
@property(copy) TRIMIDI midi;
@property(copy) TRIInputRead inputRead;
@property(copy) TRIInputWrite inputWrite;
@property(copy) TRIAssetsRead assetsRead;
@property(copy) TRIAssetsWrite assetsWrite;
@property(strong) NSMutableDictionary *document;
@property(strong) NSMutableArray *history;
@property(strong) NSMutableArray *future;
@property(strong) SNInstanceCanvas *canvas;
@property(strong) NSScrollView *inspectorScroll;
@property(strong) NSView *inspector;
@property(strong) NSTextField *status;
@property uint64_t selected;
@property double zoom;
@property NSPoint origin;
@property NSPoint menuPoint;
@property uint64_t wireSource;
@property int wirePort;
@property uint64_t moving;
@property BOOL panning;
@property NSPoint lastPoint;
@property(strong) NSDictionary *dragStart;
@property(strong) NSTimer *timer;
-(NSMutableDictionary *)node:(uint64_t)uid;
-(void)inspect;
-(void)commit:(NSDictionary *)previous;
-(void)insert:(NSMenuItem *)sender;
-(NSPoint)world:(NSPoint)point;
@end

@implementation SNInstanceEditor
-(void)drawRect:(NSRect)dirty {[NSColor.windowBackgroundColor setFill];NSRectFill(self.bounds);}
-(BOOL)isFlipped{return YES;}
-(void)dealloc {[_timer invalidate];}
-(NSMutableDictionary *)node:(uint64_t)uid {
    for(NSMutableDictionary *node in _document[@"nodes"])if([node[@"id"] unsignedLongLongValue]==uid)return node;
    return nil;
}
-(void)message:(NSString *)text {_status.stringValue=text ?: @"";}
-(void)commit:(NSDictionary *)previous {
    tri::GraphSpec graph;NSString *why=nil;
    if(!triassets::decodeGraph(_document,graph,&why) || !_assetsWrite || !_assetsWrite(@{@"graph":_document})) {
        _document=copyJSON(previous) ?: [@{@"version":@3,@"nodes":[NSMutableArray new],@"edges":[NSMutableArray new]}mutableCopy];[self message:why ?: @"宿主无法应用节点图。"];
    } else {
        if(previous && ![previous isEqual:_document]) {
            [_history addObject:copyJSON(previous)];[_future removeAllObjects];
        }
        [self message:[NSString stringWithFormat:@"%lu 个独立节点 · %lu 条连接",(unsigned long)[_document[@"nodes"] count],(unsigned long)[_document[@"edges"] count]]];
    }
    _canvas.needsDisplay=YES;
}
-(NSButton *)button:(NSString *)text frame:(NSRect)frame action:(SEL)action parent:(NSView *)parent {
    NSButton *b=[NSButton buttonWithTitle:text target:self action:action];b.frame=frame;[parent addSubview:b];return b;
}
-(void)insert:(NSMenuItem *)sender {
    NSDictionary *previous=copyJSON(_document);
    uint64_t next=1;
    for(NSDictionary *node in _document[@"nodes"])next=std::max(next,[node[@"id"] unsignedLongLongValue]+1);
    int type=(int)sender.tag;
    NSMutableDictionary *parameters=[NSMutableDictionary new];
    for(int p:controls(type))parameters[[@(p) stringValue]]=@(tri::graphParamInfo(p).def);
    if(type==tri::Osc1Node)parameters[[@(tri::Level1) stringValue]]=@.66;
    NSMutableDictionary *node=[@{@"id":@(next),@"type":@(type),@"x":@(_menuPoint.x),@"y":@(_menuPoint.y),@"parameters":parameters} mutableCopy];
    [_document[@"nodes"] addObject:node];_selected=next;
    [self commit:previous];[self inspect];
}
-(void)remove:(id)sender {
    NSDictionary *previous=copyJSON(_document);
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    [_document[@"nodes"] removeObject:node];
    NSIndexSet *indices=[_document[@"edges"] indexesOfObjectsPassingTest:^BOOL(NSDictionary *edge,NSUInteger,BOOL*){
        return [edge[@"source"] unsignedLongLongValue]==self.selected || [edge[@"target"] unsignedLongLongValue]==self.selected;
    }];
    [_document[@"edges"] removeObjectsAtIndexes:indices];_selected=0;
    [self commit:previous];[self inspect];
}
-(void)disconnect:(id)sender {
    if(![self node:_selected])return;
    NSDictionary *previous=copyJSON(_document);
    NSIndexSet *indices=[_document[@"edges"] indexesOfObjectsPassingTest:^BOOL(NSDictionary *edge,NSUInteger,BOOL*){
        return [edge[@"source"] unsignedLongLongValue]==self.selected || [edge[@"target"] unsignedLongLongValue]==self.selected;
    }];
    [_document[@"edges"] removeObjectsAtIndexes:indices];[self commit:previous];
}
-(void)duplicate:(id)sender {
    NSDictionary *previous=copyJSON(_document);
    NSMutableDictionary *original=[self node:_selected];if(!original)return;
    uint64_t next=1;
    for(NSDictionary *node in _document[@"nodes"])next=std::max(next,[node[@"id"] unsignedLongLongValue]+1);
    NSMutableDictionary *copy=copyJSON(original);copy[@"id"]=@(next);
    copy[@"x"]=@([copy[@"x"] doubleValue]+35);copy[@"y"]=@([copy[@"y"] doubleValue]+45);
    [_document[@"nodes"] addObject:copy];_selected=next;[self commit:previous];[self inspect];
}
-(void)undo:(id)sender {[self historyFrom:_history to:_future];}
-(void)redo:(id)sender {[self historyFrom:_future to:_history];}
-(void)historyFrom:(NSMutableArray *)from to:(NSMutableArray *)to {
    if(!from.count)return;
    NSDictionary *next=from.lastObject;
    if(_assetsWrite(@{@"graph":next})) {
        [to addObject:copyJSON(_document)];_document=copyJSON(next);[from removeLastObject];
        [self inspect];_canvas.needsDisplay=YES;
    }
}
-(void)changed:(NSTextField *)field {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    int p=(int)field.tag;double value=field.doubleValue;
    NSScanner *scan=[NSScanner scannerWithString:field.stringValue];
    if(![scan scanDouble:&value] || !scan.isAtEnd || !std::isfinite(value) ||
        value<tri::params[p].min || value>tri::params[p].max) { [self message:@"参数超出范围。"];return; }
    NSDictionary *previous=copyJSON(_document);
    node[@"parameters"][[@(p) stringValue]]=@(value);
    [self commit:previous];[self.window makeFirstResponder:_canvas];
}
-(void)trigger:(id)sender {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    NSDictionary *previous=copyJSON(_document);
    NSString *key=[@(tri::TriggerPulse) stringValue];
    node[@"parameters"][key]=@(([node[@"parameters"][key] intValue]+1)%1000000);
    [self commit:previous];
}
-(void)panic:(id)sender {
    NSDictionary *previous=copyJSON(_document);
    for(NSMutableDictionary *node in _document[@"nodes"])
        for(int p:{tri::ClipRun,tri::ProgressRun,tri::ChordRun})if(node[@"parameters"][[@(p) stringValue]])node[@"parameters"][[@(p) stringValue]]=@0;
    [self commit:previous];
    if(_midi)for(int ch=0;ch<16;++ch)_midi(0xb0|ch,120,0);
}
-(void)input:(NSButton *)sender {if(_inputWrite)_inputWrite(sender.state==NSControlStateValueOn);}
-(void)harmony:(NSTextField *)field {
    auto parsed=tri::parseProgression(field.stringValue.UTF8String);
    NSMutableDictionary *node=[self node:_selected];if(!parsed || !node){[self message:@"请输入和弦名称，如 Cmaj7 | Am7 | Dm7 | G7。"];return;}
    NSDictionary *previous=copyJSON(_document);NSMutableDictionary *p=node[@"parameters"];
    if([node[@"type"] intValue]==tri::ChordNode) {
        p[[@(tri::ChordRoot) stringValue]]=@((*parsed)[0].root);p[[@(tri::ChordQuality) stringValue]]=@((*parsed)[0].quality);
    } else {
        p[[@(tri::ProgressLength) stringValue]]=@(parsed->size());
        for(size_t i=0;i<parsed->size();++i){
            p[[@(tri::ProgressRoot1+2*i) stringValue]]=@((*parsed)[i].root);
            p[[@(tri::ProgressQuality1+2*i) stringValue]]=@((*parsed)[i].quality);
        }
    }
    [self commit:previous];[self inspect];
}
-(void)import:(id)sender {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    uint64_t uid=_selected;BOOL clip=[node[@"type"] intValue]==tri::ClipNode;
    NSOpenPanel *panel=[NSOpenPanel openPanel];panel.canChooseDirectories=!clip;
    NSMutableArray *types=[NSMutableArray new];
    for(NSString *ext in clip?@[@"mid",@"midi"]:@[@"mlmodel",@"mlmodelc",@"mlpackage",@"json"]) {
        UTType *type=[UTType typeWithFilenameExtension:ext];if(type)[types addObject:type];
    }
    panel.allowedContentTypes=types;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result!=NSModalResponseOK)return;
        [self message:@"正在载入素材…"];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
            NSString *why=nil;NSDictionary *asset=triassets::importAsset(clip?@"clip":@"model",panel.URL,&why);
            dispatch_async(dispatch_get_main_queue(),^{
                NSMutableDictionary *current=[self node:uid];
                if(!asset){[self message:why];return;}if(!current)return;
                NSDictionary *previous=copyJSON(self.document);current[@"asset"]=copyJSON(asset);
                [self commit:previous];[self inspect];
            });
        });
    }];
}
-(void)label:(NSString *)text y:(double)y {
    NSTextField *label=[NSTextField wrappingLabelWithString:text];
    label.frame=NSMakeRect(12,y,235,42);label.textColor=NSColor.labelColor;
    [_inspector addSubview:label];
}
-(void)rangeChanged:(NSTextField *)field {
    NSMutableDictionary *node=[self node:_selected];
    if([node[@"type"] intValue]!=tri::ArrangerNode)return;
    int pid=(int)field.tag;
    for(auto voice:voiceControls) {
        if(pid!=voice.lower && pid!=voice.upper)continue;
        NSInteger value=0;
        NSScanner *scanner=[NSScanner scannerWithString:field.stringValue];
        bool valid=[scanner scanInteger:&value] && scanner.isAtEnd && value>=0 && value<=127;
        NSInteger lower=pid==voice.lower?value:voiceValue(node,voice.lower).integerValue;
        NSInteger upper=pid==voice.upper?value:voiceValue(node,voice.upper).integerValue;
        if(!valid || lower>upper) {
            field.integerValue=voiceValue(node,pid).integerValue;
            [self message:@"音域需为 0–127 的整数，最低音不能高于最高音。"];
            return;
        }
        NSDictionary *previous=copyJSON(_document);
        node[@"parameters"][[@(pid) stringValue]]=@(value);
        [self commit:previous];[self inspect];return;
    }
}
-(void)textureChanged:(NSPopUpButton *)popup {
    NSMutableDictionary *node=[self node:_selected];
    if([node[@"type"] intValue]!=tri::ArrangerNode)return;
    NSDictionary *previous=copyJSON(_document);
    node[@"parameters"][[@(popup.tag) stringValue]]=@(popup.indexOfSelectedItem);
    [self commit:previous];[self inspect];
}
-(double)arrangerInspector:(NSDictionary *)node y:(double)y {
    [self label:@"音域用 MIDI 音号表示（60 = C4）。选出的音按八度移入各声部音域。" y:y];
    y+=46;
    for(int i=0;i<3;++i) {
        auto voice=voiceControls[i];
        NSString *name=voiceNames()[i];
        int lower=voiceValue(node,voice.lower).intValue,upper=voiceValue(node,voice.upper).intValue;
        NSTextField *heading=[NSTextField labelWithString:[NSString stringWithFormat:@"%@   %@ – %@",name,pitchName(lower),pitchName(upper)]];
        heading.font=[NSFont boldSystemFontOfSize:13];heading.frame=NSMakeRect(12,y,225,22);
        [_inspector addSubview:heading];y+=23;
        for(int bound=0;bound<2;++bound) {
            NSString *caption=bound?@"最高音":@"最低音";
            double x=bound?128:12;
            NSTextField *label=[NSTextField labelWithString:caption];
            label.frame=NSMakeRect(x,y+2,49,21);[_inspector addSubview:label];
            NSTextField *field=[[NSTextField alloc]initWithFrame:NSMakeRect(x+50,y,48,24)];
            field.integerValue=bound?upper:lower;field.tag=bound?voice.upper:voice.lower;
            field.target=self;field.action=@selector(rangeChanged:);
            [field setAccessibilityLabel:[name stringByAppendingString:caption]];
            [_inspector addSubview:field];
        }
        y+=30;
        NSTextField *label=[NSTextField labelWithString:@"织体"];
        label.frame=NSMakeRect(12,y+3,48,20);[_inspector addSubview:label];
        NSPopUpButton *popup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(60,y,175,26) pullsDown:NO];
        [popup addItemsWithTitles:@[@"最高音",@"中间音",@"最低音",@"柱式和弦（全部音）"]];
        [popup selectItemAtIndex:voiceValue(node,voice.texture).integerValue];
        popup.tag=voice.texture;popup.target=self;popup.action=@selector(textureChanged:);
        [popup setAccessibilityLabel:[name stringByAppendingString:@"织体"]];
        [_inspector addSubview:popup];y+=38;
    }
    return y;
}
-(void)inspect {
    for(NSView *view in _inspector.subviews.copy)[view removeFromSuperview];
    NSMutableDictionary *node=[self node:_selected];
    if(!node){[self label:@"右键画布插入节点。拖动输出端口接到输入端口。" y:12];return;}
    int type=[node[@"type"] intValue];
    [self label:[NSString stringWithFormat:@"%@  #%llu",title(type),_selected] y:12];
    [self button:@"复制节点" frame:NSMakeRect(12,54,105,28) action:@selector(duplicate:) parent:_inspector];
    [self button:@"删除节点" frame:NSMakeRect(132,54,105,28) action:@selector(remove:) parent:_inspector];
    double y=96;
    if(type==tri::ArrangerNode)y=[self arrangerInspector:node y:y];
    else for(int p:controls(type)) {
        [self label:parameterName(p) y:y];
        NSTextField *field=[[NSTextField alloc]initWithFrame:NSMakeRect(142,y,96,25)];
        id val=node[@"parameters"][[@(p) stringValue]] ?: @(tri::params[p].def);
        field.stringValue=[val description];field.tag=p;field.target=self;field.action=@selector(changed:);
        [_inspector addSubview:field];y+=48;
    }
    if(type==tri::StartNode || type==tri::StopNode){
        [self button:@"发送触发信号" frame:NSMakeRect(12,y,225,30) action:@selector(trigger:) parent:_inspector];y+=44;
    }
    if(type==tri::WaitNode){[self label:@"收到开始或停止信号后，等待指定秒数再转发。可排队等待多个信号。" y:y];y+=65;}
    if(type==tri::CountNode){[self label:@"每收到 X 个信号后输出一次，再开始下一轮计数。可连接 Clip 的循环完成端口。" y:y];y+=65;}
    if(type==tri::ClipNode || type==tri::ModelNode) {
        [self button:type==tri::ClipNode?@"载入 MIDI Clip…":@"载入音符模型…" frame:NSMakeRect(12,y,225,30) action:@selector(import:) parent:_inspector];y+=42;
        [self label:node[@"asset"][@"name"] ?: @"未载入素材" y:y];y+=46;
    }
    if(type==tri::ChordNode || type==tri::ProgressNode) {
        [self label:@"和弦 / 走向，可手动输入" y:y];y+=28;
        NSTextField *field=[[NSTextField alloc]initWithFrame:NSMakeRect(12,y,225,28)];
        field.placeholderString=type==tri::ChordNode?@"Cmaj7":@"Cmaj7 | Am7 | Dm7 | G7";
        field.target=self;field.action=@selector(harmony:);[_inspector addSubview:field];y+=40;
        [self button:@"预制 Cmaj7 / 爵士走向" frame:NSMakeRect(12,y,225,28) action:@selector(preset:) parent:_inspector];y+=42;
    }
    if(type==tri::InputNode && _inputRead) {
        NSButton *button=[NSButton checkboxWithTitle:@"启用硬件音频输入" target:self action:@selector(input:)];
        button.frame=NSMakeRect(12,y,230,28);button.state=_inputRead();[_inspector addSubview:button];y+=42;
    }
    if(type==tri::Osc1Node){[self label:@"Wave  0 正弦  1 方波  2 锯齿  3 三角" y:y];y+=50;}
    if(type==tri::ArpNode){[self label:@"Rhythm  0 直拍  1 Bossa Nova  2 Reggae  3 Disco  4 十六分" y:y];y+=70;}
    if(type==tri::DawClockNode){[self label:@"跟随 DAW 的 BPM、走带和定位。独立版可接收外部 MIDI Clock。" y:y];y+=70;}
    if(type==tri::DawOutputNode || type==tri::DawAudioNode){[self label:@"在 DAW 插件中连接对应音频总线。" y:y];y+=50;}
    if(type==tri::HardwareOutputNode){[self label:@"在独立版输出至系统选择的声卡。" y:y];y+=50;}
    if(type==tri::MidiOutNode){[self label:@"独立版发送至 Soundnet MIDI Out；AU 使用宿主的 MIDI 输出回调。" y:y];y+=60;}
    _inspector.frame=NSMakeRect(0,0,260,std::max(520.,y+20));
}
-(void)preset:(id)sender {
    NSTextField *field=[NSTextField new];field.stringValue=[[self node:_selected][@"type"] intValue]==tri::ChordNode?@"Cmaj7":@"Dm7 | G7 | Cmaj7";
    [self harmony:field];
}
-(void)save:(id)sender {
    NSSavePanel *panel=[NSSavePanel savePanel];panel.nameFieldStringValue=@"Soundnet.json";
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result==NSModalResponseOK)[[NSJSONSerialization dataWithJSONObject:self.document options:NSJSONWritingPrettyPrinted error:nil] writeToURL:panel.URL atomically:YES];
    }];
}
-(void)open:(id)sender {
    NSOpenPanel *panel=[NSOpenPanel openPanel];panel.allowedContentTypes=@[UTTypeJSON];
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result!=NSModalResponseOK)return;
        NSDictionary *file=[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfURL:panel.URL] options:NSJSONReadingMutableContainers error:nil];
        if(![file[@"version"] isEqual:@3])file=[self migrate:file];
        tri::GraphSpec graph;NSString *why=nil;
        if(!file || !triassets::decodeGraph(file,graph,&why)){[self message:why ?: @"无效的工作流。"];return;}
        NSDictionary *previous=copyJSON(self.document);self.document=copyJSON(file);
        [self commit:previous];[self fit:nil];[self inspect];
    }];
}
-(NSDictionary *)migrate:(NSDictionary *)file {
    NSString *why=nil;
    if(!ternaryfile::validate(file,&why))return nil;
    NSMutableArray *nodes=[NSMutableArray new],*edges=[NSMutableArray new];
    NSArray *names=ternaryfile::names();
    NSMutableSet *ids=[NSMutableSet new];
    NSDictionary *parameters=file[@"parameters"];
    for(NSString *name in file[@"nodes"]) {
        NSUInteger uid=[names indexOfObject:name];if(uid==NSNotFound)continue;
        int type=(int)uid;if(type==4 || type==5)type=3;
        if(type==tri::OutputNode)type=_inputRead?tri::HardwareOutputNode:tri::DawOutputNode;
        if(type==tri::MidiNode && !_inputRead)type=tri::DawMidiNode;
        if(type==tri::InputNode && !_inputRead)type=tri::DawAudioNode;
        if(type==tri::ClockNode && [parameters[@(tri::params[tri::ClockSync].name)] floatValue]>.5)type=tri::DawClockNode;
        NSMutableDictionary *p=[NSMutableDictionary new];
        for(int pid:controls(type))p[[@(pid) stringValue]]=parameters[@(tri::params[pid].name)] ?: @(tri::params[pid].def);
        if(uid>=3 && uid<=5) {
            int lane=(int)uid-3;
            for(int i=0;i<3;++i)p[[@(tri::Wave1+i) stringValue]]=parameters[@(tri::params[tri::Wave1+3*lane+i].name)] ?: @(tri::params[tri::Wave1+i].def);
            for(int i=0;i<3;++i) {
                float v=[parameters[@(tri::params[tri::Osc1Attack+3*lane+i].name)] floatValue];
                int pid=i==0?tri::Attack:i==1?tri::Sustain:tri::Release;
                if(v>=0)p[[@(pid) stringValue]]=@(v);
            }
        }
        if(type==tri::ProgressNode)for(int pid=tri::ProgressRoot1;pid<=tri::ProgressQuality8;++pid)
            p[[@(pid) stringValue]]=parameters[@(tri::params[pid].name)] ?: @(tri::params[pid].def);
        NSArray *pos=file[@"nodes"][name];
        if([pos[0] doubleValue]<0 || [pos[1] doubleValue]<0) {
            static const double x[]={0,0,210,420,420,420,420,640,850,1060,1270};
            static const double y[]={0,160,160,0,160,320,480,160,160,160,160};
            pos=uid<=10?@[@(x[uid]),@(y[uid])]:@[@((uid-11)*210),@650];
        }
        NSMutableDictionary *node=[@{@"id":@(uid),@"type":@(type),@"x":@([pos[0] floatValue]),@"y":@([pos[1] floatValue]),@"parameters":p}mutableCopy];
        id asset=file[@"assets"][type==tri::ClipNode?@"clip":@"model"];
        if((type==tri::ClipNode || type==tri::ModelNode)&&asset)node[@"asset"]=asset;
        [nodes addObject:node];[ids addObject:@(uid)];
    }
    for(NSString *port in file[@"connections"]) {
        NSUInteger index=[ternaryfile::ports() indexOfObject:port];if(index==NSNotFound)continue;
        int pid=tri::routeID((int)index),target=tri::routeTarget(pid);
        NSUInteger source=[names indexOfObject:file[@"connections"][port]];
        if(![ids containsObject:@(target)] || ![ids containsObject:@(source)])continue;
        int slot=pid>=tri::RouteMix1&&pid<=tri::RouteMix4?pid-tri::RouteMix1:tri::clockPort(pid)&&(target==tri::ArpNode||target==tri::VelocityNode)?1:0;
        [edges addObject:[@{@"source":@(source),@"target":@(target),@"port":@(slot)}mutableCopy]];
    }
    return @{@"version":@3,@"nodes":nodes,@"edges":edges};
}
-(NSPoint)world:(NSPoint)p{return NSMakePoint((p.x-_origin.x)/_zoom,(p.y-_origin.y)/_zoom);}
-(void)fit:(id)sender {
    NSRect all=NSZeroRect;
    for(NSDictionary *node in _document[@"nodes"]) {
        NSRect r=NSMakeRect([node[@"x"] doubleValue],[node[@"y"] doubleValue],170,130);
        all=NSIsEmptyRect(all)?r:NSUnionRect(all,r);
    }
    if(NSIsEmptyRect(all))all=NSMakeRect(0,0,700,400);
    _zoom=std::clamp(std::min(800./all.size.width,450./all.size.height),.1,1.2);
    _origin=NSMakePoint(20-all.origin.x*_zoom,35-all.origin.y*_zoom);_canvas.needsDisplay=YES;
}
-(void)blank:(id)sender {
    NSDictionary *previous=copyJSON(_document);
    _document=[@{@"version":@3,@"nodes":[NSMutableArray new],@"edges":[NSMutableArray new]}mutableCopy];
    _selected=0;[self commit:previous];[self inspect];
}
-(void)tick {
    NSDictionary *remote=_assetsRead ? _assetsRead()[@"graph"] : nil;
    if(remote && ![remote isEqual:_document] && !_moving && !_wireSource) {
        _document=copyJSON(remote);[self inspect];_canvas.needsDisplay=YES;
    }
}
@end

namespace {
NSRect nodeRect(NSDictionary *node) {
    int type=[node[@"type"] intValue];
    int count=std::max((int)tri::inputTypes(type).size(),tri::outputCount(type));
    return NSMakeRect([node[@"x"] doubleValue],[node[@"y"] doubleValue],172,std::max(88,48+count*24));
}
NSPoint jack(NSDictionary *node,bool out,int port) {
    NSRect r=nodeRect(node);
    return NSMakePoint(out?NSMaxX(r):NSMinX(r),NSMinY(r)+47+24*port);
}
void drawText(NSString *text,NSRect rect,NSColor *color,double size=12) {
    [text drawInRect:rect withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:size],
        NSForegroundColorAttributeName:color}];
}
}
@implementation SNInstanceCanvas
-(BOOL)isFlipped{return YES;}
-(BOOL)acceptsFirstResponder{return YES;}
-(void)drawRect:(NSRect)dirty {
    [[NSColor colorWithWhite:.09 alpha:1]setFill];NSRectFill(self.bounds);
    SNInstanceEditor *editor=_editor;
    [NSGraphicsContext saveGraphicsState];
    NSBezierPath *clip=[NSBezierPath bezierPathWithRect:self.bounds];[clip addClip];
    NSAffineTransform *transform=[NSAffineTransform transform];
    [transform translateXBy:editor.origin.x yBy:editor.origin.y];[transform scaleBy:editor.zoom];[transform concat];
    for(NSDictionary *edge in editor.document[@"edges"]) {
        NSDictionary *a=[editor node:[edge[@"source"] unsignedLongLongValue]],*b=[editor node:[edge[@"target"] unsignedLongLongValue]];
        int port=[edge[@"port"] intValue],out=[edge[@"sourcePort"] intValue];
        NSPoint from=jack(a,true,out),to=jack(b,false,port);
        NSBezierPath *path=[NSBezierPath new];[path moveToPoint:from];
        [path curveToPoint:to controlPoint1:NSMakePoint(from.x+70,from.y) controlPoint2:NSMakePoint(to.x-70,to.y)];
        [signalColor(tri::outputType([a[@"type"] intValue],out))setStroke];path.lineWidth=2;[path stroke];
    }
    for(NSDictionary *node in editor.document[@"nodes"]) {
        int type=[node[@"type"] intValue];NSRect r=nodeRect(node);
        [[NSColor colorWithWhite:.18 alpha:1]setFill];[[NSBezierPath bezierPathWithRoundedRect:r xRadius:4 yRadius:4]fill];
        [[node[@"id"] unsignedLongLongValue]==editor.selected?NSColor.whiteColor:[NSColor colorWithWhite:.4 alpha:1] setStroke];
        [[NSBezierPath bezierPathWithRect:r]stroke];
        drawText([NSString stringWithFormat:@"%@ #%@",title(type),node[@"id"]],NSMakeRect(r.origin.x+9,r.origin.y+8,160,28),NSColor.whiteColor);
        auto inputs=tri::inputTypes(type);
        for(int p=0;p<(int)inputs.size();++p) {
            NSPoint at=jack(node,false,p);[signalColor(inputs[p])setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(at.x-5,at.y-5,10,10)]fill];
            NSString *label=inputs[p]==tri::Signal::Audio?@"音频":inputs[p]==tri::Signal::Notes?@"音符":inputs[p]==tri::Signal::Trigger?@"触发":@"时钟";
            drawText(label,NSMakeRect(at.x+9,at.y-8,55,20),NSColor.lightGrayColor);
        }
        int outputs=tri::outputCount(type);
        for(int p=0;p<outputs;++p) {
            NSPoint at=jack(node,true,p);[signalColor(tri::outputType(type,p))setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(at.x-5,at.y-5,10,10)]fill];
            drawText(outputName(type,p),NSMakeRect(at.x-68,at.y-8,62,20),NSColor.lightGrayColor);
        }
    }
    [NSGraphicsContext restoreGraphicsState];
}
-(void)rightMouseDown:(NSEvent *)event {
    SNInstanceEditor *editor=_editor;
    editor.menuPoint=[editor world:[self convertPoint:event.locationInWindow fromView:nil]];
    NSMenu *menu=[NSMenu new];
    for(NSString *group in @[@"输入",@"处理",@"逻辑",@"输出"]) {
        NSMenuItem *category=[menu addItemWithTitle:group action:nil keyEquivalent:@""];
        category.submenu=[NSMenu new];
        for(NSArray *row in catalog())if([row[2] isEqual:group]) {
            NSMenuItem *item=[category.submenu addItemWithTitle:row[1] action:@selector(insert:) keyEquivalent:@""];
            item.target=editor;item.tag=[row[0] integerValue];
        }
    }
    [menu addItem:[NSMenuItem separatorItem]];
    for(NSArray *item in @[@[@"复制选中节点",@"duplicate:"],@[@"断开选中节点",@"disconnect:"],@[@"删除选中节点",@"remove:"]]) {
        NSMenuItem *action=[menu addItemWithTitle:item[0] action:NSSelectorFromString(item[1]) keyEquivalent:@""];
        action.target=editor;action.enabled=[editor node:editor.selected]!=nil;
    }
    [NSMenu popUpContextMenu:menu withEvent:event forView:self];
}
-(void)mouseDown:(NSEvent *)event {
    SNInstanceEditor *editor=_editor;[self.window makeFirstResponder:self];
    NSPoint view=[self convertPoint:event.locationInWindow fromView:nil],point=[editor world:view];
    editor.lastPoint=view;editor.dragStart=copyJSON(editor.document);
    for(NSDictionary *node in [editor.document[@"nodes"] reverseObjectEnumerator]) {
        int type=[node[@"type"] intValue];
        int outputs=tri::outputCount(type);
        for(int p=0;p<outputs;++p) {
            NSPoint pos=jack(node,true,p);
            if(std::hypot(pos.x-point.x,pos.y-point.y)<12/editor.zoom) {
                editor.wireSource=[node[@"id"] unsignedLongLongValue];editor.wirePort=p;return;
            }
        }
        if(NSPointInRect(point,nodeRect(node))) {
            editor.selected=[node[@"id"] unsignedLongLongValue];editor.moving=editor.selected;
            [editor inspect];self.needsDisplay=YES;return;
        }
    }
    editor.panning=YES;
}
-(void)mouseDragged:(NSEvent *)event {
    SNInstanceEditor *editor=_editor;
    NSPoint view=[self convertPoint:event.locationInWindow fromView:nil];
    double dx=view.x-editor.lastPoint.x,dy=view.y-editor.lastPoint.y;
    if(editor.moving) {
        NSMutableDictionary *node=[editor node:editor.moving];
        node[@"x"]=@([node[@"x"] doubleValue]+dx/editor.zoom);node[@"y"]=@([node[@"y"] doubleValue]+dy/editor.zoom);
    } else if(editor.panning)editor.origin=NSMakePoint(editor.origin.x+dx,editor.origin.y+dy);
    editor.lastPoint=view;self.needsDisplay=YES;
}
-(void)mouseUp:(NSEvent *)event {
    SNInstanceEditor *editor=_editor;
    if(editor.wireSource) {
        NSPoint point=[editor world:[self convertPoint:event.locationInWindow fromView:nil]];
        for(NSDictionary *node in editor.document[@"nodes"]) {
            auto inputs=tri::inputTypes([node[@"type"] intValue]);
            for(int p=0;p<(int)inputs.size();++p) {
                NSPoint pos=jack(node,false,p);
                if(std::hypot(pos.x-point.x,pos.y-point.y)>14/editor.zoom)continue;
                uint64_t uid=[node[@"id"] unsignedLongLongValue];
                NSIndexSet *existing=[editor.document[@"edges"] indexesOfObjectsPassingTest:^BOOL(NSDictionary *e,NSUInteger,BOOL*){
                    return [e[@"target"] unsignedLongLongValue]==uid && [e[@"port"] intValue]==p;
                }];
                if(inputs[p]!=tri::Signal::Trigger)[editor.document[@"edges"] removeObjectsAtIndexes:existing];
                [editor.document[@"edges"] addObject:[@{@"source":@(editor.wireSource),@"target":@(uid),@"port":@(p),@"sourcePort":@(editor.wirePort)}mutableCopy]];
                [editor commit:editor.dragStart];editor.wireSource=0;return;
            }
        }
    }
    if(editor.moving)[editor commit:editor.dragStart];
    editor.wireSource=0;editor.moving=0;editor.panning=NO;
}
-(void)scrollWheel:(NSEvent *)event {
    if(event.modifierFlags&NSEventModifierFlagCommand) _editor.zoom=std::clamp(_editor.zoom*std::exp(-event.scrollingDeltaY*.02),.05,3.);
    else _editor.origin=NSMakePoint(_editor.origin.x-event.scrollingDeltaX,_editor.origin.y-event.scrollingDeltaY);
    self.needsDisplay=YES;
}
-(void)keyDown:(NSEvent *)event {
    if(event.keyCode==51 || event.keyCode==117)[_editor remove:nil];else [super keyDown:event];
}
@end

@interface SNFlippedView : NSView
@end
@implementation SNFlippedView
-(BOOL)isFlipped{return YES;}
@end
@interface SNPiano : NSView
@property(copy) TRIMIDI send;
@property int note;
@end
@implementation SNPiano
-(BOOL)isFlipped{return YES;}
-(void)drawRect:(NSRect)dirty {
    for(int i=0;i<24;++i) {
        NSRect r=NSMakeRect(i*self.bounds.size.width/24,0,self.bounds.size.width/24-1,55);
        [(i+48)==_note?NSColor.systemOrangeColor:NSColor.lightGrayColor setFill];NSRectFill(r);
        drawText([NSString stringWithFormat:@"%d",48+i],NSInsetRect(r,4,18),NSColor.blackColor,10);
    }
}
-(void)mouseDown:(NSEvent *)event {
    NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];
    _note=48+std::clamp(int(p.x/self.bounds.size.width*24),0,23);
    if(_send)_send(0x90,_note,100);self.needsDisplay=YES;
}
-(void)mouseUp:(NSEvent *)event {if(_note && _send)_send(0x80,_note,0);_note=0;self.needsDisplay=YES;}
@end

NSView *TRICreateEditor(TRIRead read,TRIWrite write,TRIMIDI midi,TRIInputRead inputRead,TRIInputWrite inputWrite,TRIStatus status,TRIAssetsRead assetsRead,TRIAssetsWrite assetsWrite) {
    SNInstanceEditor *editor=[[SNInstanceEditor alloc]initWithFrame:NSMakeRect(0,0,1144,650)];
    editor.read=read;editor.write=write;editor.midi=midi;editor.inputRead=inputRead;editor.inputWrite=inputWrite;
    editor.assetsRead=assetsRead;editor.assetsWrite=assetsWrite;
    editor.history=[NSMutableArray new];editor.future=[NSMutableArray new];editor.zoom=1;editor.origin=NSMakePoint(20,20);
    [editor button:@"打开" frame:NSMakeRect(12,10,65,28) action:@selector(open:) parent:editor];
    [editor button:@"保存" frame:NSMakeRect(82,10,65,28) action:@selector(save:) parent:editor];
    [editor button:@"撤销" frame:NSMakeRect(152,10,65,28) action:@selector(undo:) parent:editor];
    [editor button:@"重做" frame:NSMakeRect(222,10,65,28) action:@selector(redo:) parent:editor];
    [editor button:@"适配" frame:NSMakeRect(292,10,65,28) action:@selector(fit:) parent:editor];
    [editor button:@"清空" frame:NSMakeRect(362,10,65,28) action:@selector(blank:) parent:editor];
    [editor button:@"停音" frame:NSMakeRect(432,10,65,28) action:@selector(panic:) parent:editor];
    NSTextField *heading=[NSTextField labelWithString:@"Soundnet 2.1    输入 → 处理 → 输出"];
    heading.frame=NSMakeRect(520,15,600,25);[editor addSubview:heading];
    SNInstanceCanvas *canvas=[[SNInstanceCanvas alloc]initWithFrame:NSMakeRect(8,48,862,505)];
    canvas.editor=editor;editor.canvas=canvas;[editor addSubview:canvas];
    NSScrollView *scroll=[[NSScrollView alloc]initWithFrame:NSMakeRect(878,48,260,505)];
    scroll.hasVerticalScroller=YES;editor.inspectorScroll=scroll;
    editor.inspector=[[SNFlippedView alloc]initWithFrame:NSMakeRect(0,0,260,520)];
    scroll.documentView=editor.inspector;[editor addSubview:scroll];
    editor.status=[NSTextField labelWithString:@"右键插入节点 · 音符 / 音频 / 时钟 / 触发信号"];
    editor.status.frame=NSMakeRect(12,558,1120,22);[editor addSubview:editor.status];
    SNPiano *piano=[[SNPiano alloc]initWithFrame:NSMakeRect(12,590,1120,55)];piano.send=midi;[editor addSubview:piano];
    NSDictionary *assets=assetsRead?assetsRead():nil;
    if(assets[@"graph"])editor.document=copyJSON(assets[@"graph"]);
    else {
        NSDictionary *legacy=ternaryfile::snapshot(read,assetsRead);
        editor.document=copyJSON([editor migrate:legacy] ?: @{@"version":@3,@"nodes":@[],@"edges":@[]});
        if(assets)[editor commit:nil];
    }
    [editor fit:nil];[editor inspect];
    __weak SNInstanceEditor *weak=editor;
    editor.timer=[NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer*){[weak tick];}];
    return editor;
}
