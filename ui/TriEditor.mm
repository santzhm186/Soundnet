#import "TriEditor.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import "WorkflowCanvas.h"
#import "WorkflowFile.hpp"
#include "../src/Engine.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr int width = 1144, height = 650;
constexpr unsigned background = 0x222426, panel = 0x2a2d2f, border = 0x505456;
constexpr unsigned foreground = 0xe3e6e5, muted = 0xb2b9b7, active = 0xadd0bd;

NSColor *color(unsigned rgb) {
    return [NSColor colorWithSRGBRed:((rgb >> 16) & 255) / 255.
                               green:((rgb >> 8) & 255) / 255.
                                blue:(rgb & 255) / 255. alpha:1];
}
NSFont *font(CGFloat size, bool bold = false, bool mono = false) {
    auto weight = bold ? NSFontWeightSemibold : NSFontWeightRegular;
    return mono ? [NSFont monospacedSystemFontOfSize:size weight:weight]
                : [NSFont systemFontOfSize:size weight:weight];
}
void fill(NSRect rect, unsigned shade) {
    [color(shade) setFill];
    NSRectFill(rect);
}
void box(NSRect rect) {
    fill(rect, border);
    fill(NSInsetRect(rect, 1, 1), panel);
}
void text(NSString *string, NSRect rect, CGFloat size = 12, unsigned shade = foreground,
          bool bold = false, bool mono = false) {
    NSMutableParagraphStyle *style = [NSMutableParagraphStyle new];
    style.lineBreakMode = NSLineBreakByTruncatingTail;
    [string drawInRect:rect withAttributes:@{NSFontAttributeName:font(size, bold, mono),
        NSForegroundColorAttributeName:color(shade), NSParagraphStyleAttributeName:style}];
}
void heading(NSString *title, int x, int y) {
    text(title, NSMakeRect(x, y, 230, 22), 14, foreground, true);
}
int globalFor(int id) {
    if (id < tri::Osc1Attack || id > tri::Osc3Release) return -1;
    constexpr int ids[] = {tri::Attack, tri::Sustain, tri::Release};
    return ids[(id - tri::Osc1Attack) % 3];
}
// The editor always shows milliseconds and percentages, including when a value
// follows the shared envelope. The AU and DSP store seconds and linear gain.
double displayScale(int id) {
    int source = globalFor(id);
    switch (source == -1 ? id : source) {
        case tri::Attack: case tri::Decay: case tri::Release: return 1000;
        case tri::Level1: case tri::Level2: case tri::Level3: case tri::Sustain:
        case tri::InputGain: case tri::DelayFeedback: case tri::DelayMix:
        case tri::Gate: case tri::Master: return 100;
        default: return 1;
    }
}
bool isTuning(int id) { return id == tri::Tune1 || id == tri::Tune2 || id == tri::Tune3; }
NSString *numberText(double value) {
    if (std::abs(value - std::round(value)) < .0005)
        return [NSString stringWithFormat:@"%.0f", std::round(value)];
    return [NSString stringWithFormat:@"%.1f", value];
}
NSRect parameterRect(int x, int y, int width) { return NSMakeRect(x, y, width, 24); }
constexpr int side = 877;
constexpr int envelopeInspector = tri::layout::nodeCount+1;
constexpr int bit(int node) { return 1 << (node-1); }
NSArray<NSString*> *nodeTitles() {
    return @[@"画布", @"本地 MIDI 输入", @"ARP 琶音器", @"振荡器 1", @"振荡器 2", @"振荡器 3",
             @"外部音频", @"混音器", @"三段均衡", @"立体声延迟", @"主输出", @"和弦输入", @"和声走向", @"节奏速度", @"力度自动化", @"DAW MIDI 输入", @"MIDI Clip 输入", @"音符模型", @"MIDI 输出"];
}
NSArray<NSString*> *codes() { return @[@"", @"MIDI IN", @"ARP", @"OSC 1", @"OSC 2", @"OSC 3", @"AUDIO IN", @"MIX", @"EQ", @"DELAY", @"OUT", @"CHORD", @"PROGRESSION", @"CLOCK", @"VELOCITY", @"DAW MIDI", @"MIDI CLIP", @"NOTE MODEL", @"MIDI OUT"]; }
}

@interface TRNKeys : NSView
@property(copy) TRIMIDI send; @property int held;
@end
@implementation TRNKeys
-(BOOL)isFlipped{return YES;}
-(int)pitch:(int)idx{static const int pitches[]={0,2,4,5,7,9,11};return 48+12*(idx/7)+pitches[idx%7];}
-(void)drawRect:(NSRect)dirty{CGFloat w=NSWidth(self.bounds)/21., h=NSHeight(self.bounds);
 for(int j=0;j<21;j++){int p=[self pitch:j];NSRect r=NSMakeRect(j*w,0,w-1.5,h);fill(r,_held==p?active:0xd7d9d8);if(j%7==0)text([NSString stringWithFormat:@"C%d",3+j/7],NSMakeRect(j*w+5,h-16,w-6,14),9,0x535b54,true,true);}
 for(int j=0;j<20;j++){int p=[self pitch:j];if(j%7==2||j%7==6)continue;fill(NSMakeRect((j+1)*w-w*.31,0,w*.62,h*.59),_held==p+1?active:0x242628);}
}
-(int)noteAt:(NSPoint)pt{if(!NSPointInRect(pt,self.bounds))return -1;CGFloat w=NSWidth(self.bounds)/21.;if(pt.y<NSHeight(self.bounds)*.59)for(int j=0;j<20;j++){if(j%7!=2&&j%7!=6&&fabs(pt.x-(j+1)*w)<w*.31)return [self pitch:j]+1;}return [self pitch:std::clamp(int(pt.x/w),0,20)];}
-(void)note:(int)n{if(n==_held)return;if(_held>=0&&_send)_send(0x80,_held,0);_held=n;if(n>=0&&_send)_send(0x90,n,96);self.needsDisplay=YES;}
-(void)mouseDown:(NSEvent*)e{[self note:[self noteAt:[self convertPoint:e.locationInWindow fromView:nil]]];}
-(void)mouseDragged:(NSEvent*)e{[self mouseDown:e];}
-(void)mouseUp:(NSEvent*)e{[self note:-1];}
-(void)viewWillMoveToWindow:(NSWindow*)w{if(!w)[self note:-1];[super viewWillMoveToWindow:w];}
@end

@interface TRNEditor : NSView <NSTextFieldDelegate>
@property(copy) TRIRead read;
@property(copy) TRIAssetsRead assetsRead;
@property(copy) TRIAssetsWrite assetsWrite;
@property(copy) TRIWrite write;
@property(copy) TRIMIDI send;
@property(copy) TRIInputRead inputRead;
@property(copy) TRIInputWrite inputWrite;
@property(copy) TRIStatus status;
@property(strong) NSMutableDictionary<NSNumber*, NSTextField*> *fields;
@property(strong) NSMutableDictionary<NSNumber*, NSStepper*> *steppers;
@property(strong) NSMutableArray<NSPopUpButton*> *menus;
@property(strong) NSButton *arpButton, *inputButton;
@property(strong) NSTimer *timer;
@property(strong) NSPopUpButton *patch;
@property(copy) NSString *statusText;
@property(strong) TRNKeys *keys;
@property(strong) TRIWorkflowCanvas *canvas;
@property(strong) NSPopUpButton *nodeMenu, *addMenu, *workflowMenu;
@property(strong) NSButton *undoButton, *redoButton;
@property(strong) NSMutableArray<NSView*> *inspectorViews;
@property(strong) NSMutableArray<NSArray*> *undoStack, *redoStack;
@property(strong) NSDictionary *editStart;
@property(copy) NSString *workflowName;
@property NSInteger inspected, visibility;
@property(strong) NSTextField *symbols;
@property(strong) NSButton *musicPlay,*clipLoop;
@property(strong) NSTextField *assetName;
@property(strong) NSButton *importButton;
@property(copy) NSString *loadingKind;
@property NSUInteger importGeneration;
@property double nextAssetRefresh;
@property BOOL buildingInspector;
@end

@implementation TRNEditor
-(BOOL)isFlipped { return YES; }
-(BOOL)isOpaque { return YES; }
-(BOOL)acceptsFirstResponder { return YES; }
// Return commits the number and puts the standalone computer keyboard back in play mode.
// AppKit handles Tab itself so that editing can continue in the next field.
-(BOOL)control:(NSControl *)control textView:(NSTextView *)view doCommandBySelector:(SEL)command {
    if (command == @selector(insertNewline:) && control==_symbols) { [self symbolsChanged:(NSTextField*)control]; [self.window makeFirstResponder:self]; return YES; }
    if (command == @selector(insertNewline:)) {
        [self.window makeFirstResponder:self];
        return YES;
    }
    if (command == @selector(cancelOperation:)) {
        [control abortEditing];
        [self.window makeFirstResponder:self];
        [self syncField:(int)control.tag forced:YES];
        return YES;
    }
    return NO;
}

-(NSPopUpButton *)popup:(NSRect)rect items:(NSArray<NSString*> *)items id:(int)pid action:(SEL)action {
    NSPopUpButton *popup = [[NSPopUpButton alloc] initWithFrame:rect pullsDown:NO];
    popup.controlSize = NSControlSizeSmall;
    popup.font = font(12);
    [popup addItemsWithTitles:items];
    popup.tag = pid;
    popup.target = self;
    popup.action = action;
    if (pid >= 0) {
        popup.identifier = [NSString stringWithFormat:@"param.%d.menu", pid];
        [popup setAccessibilityLabel:@(tri::params[pid].name)];
        [_menus addObject:popup];
    }
    [self addSubview:popup];
    if (_buildingInspector) [_inspectorViews addObject:popup];
    return popup;
}
-(NSButton *)button:(NSRect)rect title:(NSString *)title action:(SEL)action {
    NSButton *button = [NSButton buttonWithTitle:title target:self action:action];
    button.frame = rect;
    button.controlSize = NSControlSizeSmall;
    button.bezelStyle = NSBezelStyleRounded;
    button.font = font(12);
    [self addSubview:button];
    if (_buildingInspector) [_inspectorViews addObject:button];
    return button;
}
-(void)addNumber:(int)pid rect:(NSRect)rect {
    const auto &spec = tri::params[pid];
    NSString *name = @(spec.name);
    NSTextField *field = [[NSTextField alloc] initWithFrame:
                          NSMakeRect(rect.origin.x, rect.origin.y + 1, rect.size.width - 21, 22)];
    field.tag = pid;
    field.identifier = [NSString stringWithFormat:@"param.%d.field", pid];
    field.font = font(13, false, true);
    field.alignment = NSTextAlignmentRight;
    field.delegate = self;
    field.target = self;
    field.action = @selector(numberChanged:);
    [(NSActionCell *)field.cell setSendsActionOnEndEditing:YES];
    [field setAccessibilityLabel:name];
    [self addSubview:field];
    if (_buildingInspector) [_inspectorViews addObject:field];
    _fields[@(pid)] = field;

    NSStepper *stepper = [[NSStepper alloc] initWithFrame:
                          NSMakeRect(NSMaxX(rect) - 20, rect.origin.y, 18, 24)];
    stepper.tag = pid;
    stepper.identifier = [NSString stringWithFormat:@"param.%d.stepper", pid];
    stepper.controlSize = NSControlSizeSmall;
    double scale = displayScale(pid);
    stepper.minValue = globalFor(pid) < 0 ? spec.min * scale : 0;
    stepper.maxValue = spec.max * scale;
    stepper.increment = (pid >= tri::EQLow && pid <= tri::EQHigh) ? .5 : 1;
    stepper.valueWraps = NO;
    stepper.autorepeat = YES;
    stepper.target = self;
    stepper.action = @selector(stepChanged:);
    [stepper setAccessibilityLabel:name];
    field.toolTip = [NSString stringWithFormat:@"%@  ·  %@ … %@%@", name,
                     numberText(stepper.minValue), numberText(stepper.maxValue),
                     globalFor(pid) >= 0 ? @"；清空后回车，跟随共享包络" : @""];
    [self addSubview:stepper];
    if (_buildingInspector) [_inspectorViews addObject:stepper];
    _steppers[@(pid)] = stepper;
}
-(double)displayValue:(int)pid {
    float value = _read(pid);
    if (value < 0 && globalFor(pid) >= 0) value = _read(globalFor(pid));
    return value * displayScale(pid);
}
-(void)syncField:(int)pid forced:(BOOL)force {
    NSTextField *field = _fields[@(pid)];
    if (!force && field.currentEditor) return;
    BOOL follow = globalFor(pid) >= 0 && _read(pid) < 0;
    double effective = [self displayValue:pid];
    NSString *display = numberText(effective);
    // The field stays empty in linked mode, so pressing Tab does not set an override.
    NSString *placeholder = follow ? [@"↳ " stringByAppendingString:display] : @"";
    if (![field.placeholderString isEqualToString:placeholder]) {
        NSMutableParagraphStyle *alignment = [NSMutableParagraphStyle new];
        alignment.alignment = NSTextAlignmentRight;
        field.placeholderAttributedString = [[NSAttributedString alloc] initWithString:placeholder
            attributes:@{NSForegroundColorAttributeName:color(0xb4c3ba), NSFontAttributeName:field.font,
                         NSParagraphStyleAttributeName:alignment}];
    }
    NSString *string = follow ? @"" : display;
    if (![field.stringValue isEqualToString:string]) field.stringValue = string;
    if (_steppers[@(pid)].doubleValue != effective) _steppers[@(pid)].doubleValue = effective;
    [field setAccessibilityHelp:follow ? [NSString stringWithFormat:@"跟随共享值 %@", display] : field.toolTip];
}
-(void)change:(int)pid value:(float)value {
    if (_read(pid) == value) return;
    [self editParam:pid value:value];
    [_patch selectItemAtIndex:0];
}
-(void)numberChanged:(NSTextField *)field {
    const int pid = (int)field.tag;
    NSString *s = [field.stringValue stringByTrimmingCharactersInSet:
                   NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (s.length == 0 && globalFor(pid) >= 0) {
        [self change:pid value:tri::Follow];
    } else {
        NSScanner *scanner = [NSScanner scannerWithString:[s stringByReplacingOccurrencesOfString:@"," withString:@"."]];
        scanner.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
        double v = 0;
        if ([scanner scanDouble:&v] && scanner.isAtEnd && std::isfinite(v)) {
            NSStepper *stepper = _steppers[@(pid)];
            v = std::clamp(v, stepper.minValue, stepper.maxValue);
            if (isTuning(pid) || pid==tri::ProgressBars || pid==tri::ClockBPM || pid==tri::VelocityMin || pid==tri::VelocityMax) v = std::round(v);
            [self change:pid value:static_cast<float>(v / displayScale(pid))];
        } else {
            NSBeep();
        }
    }
    [self syncField:pid forced:YES];
    [self refresh];
}
-(void)stepChanged:(NSStepper *)stepper {
    int pid = (int)stepper.tag;
    [self change:pid value:stepper.doubleValue / displayScale(pid)];
    [self syncField:pid forced:YES];
    [self refresh];
}
-(void)selectMenu:(NSPopUpButton *)button {
    int pid = (int)button.tag;
    [self change:pid value:button.indexOfSelectedItem + (pid == tri::ArpRepeat ? 1 : 0)];
    self.needsDisplay = YES;
}
-(void)toggleArp:(NSButton *)sender {
    [self change:tri::ArpOn value:sender.state == NSControlStateValueOn ? 1.f : 0.f];
}
-(void)toggleInput:(id)sender {
    if (_inputWrite && _inputRead) _inputWrite(!_inputRead());
    [self refresh];
}
-(void)panic:(id)sender {
    for (int param : {tri::ChordRun,tri::ProgressRun,tri::ClipRun})
        if (_read(param)>.5f) _write(param,0);
    if (_send) for (int ch = 0; ch < 16; ch++) _send(0xb0 | ch, 120, 0);
    [_keys note:-1];
}
-(void)applyPatch:(NSPopUpButton*)b{
 NSInteger idx=b.indexOfSelectedItem;if(idx<1)return;[self remember];[self panic:nil];
 // Preserve audio input and output levels when changing sounds.
 for(int p=0;p<tri::RouteArpIn;p++)if(p!=tri::Master&&p!=tri::InputGain)_write(p,tri::params[p].def);
 if(idx==2){_write(tri::Wave1,0);_write(tri::Wave2,3);_write(tri::Wave3,0);_write(tri::Tune2,12);_write(tri::Tune3,24);_write(tri::Level2,.24);_write(tri::Level3,.08);_write(tri::Decay,.34);_write(tri::Sustain,.22);_write(tri::Release,.24);_write(tri::DelayMix,.16);}
 if(idx==3){_write(tri::Wave1,3);_write(tri::Wave2,0);_write(tri::Wave3,2);_write(tri::Tune2,-12);_write(tri::Tune3,0);_write(tri::Level2,.4);_write(tri::Level3,.09);_write(tri::Decay,.18);_write(tri::Sustain,.45);_write(tri::Release,.09);_write(tri::EQLow,2);_write(tri::DelayMix,0);}
 if(idx==4){_write(tri::Wave1,3);_write(tri::Wave2,0);_write(tri::Tune2,7);_write(tri::Level3,.09);_write(tri::Attack,.45);_write(tri::Release,1.8);_write(tri::DelayTime,410);_write(tri::DelayMix,.32);}
 [self refresh];
}
-(void)finishEdit {
    if (!_editStart) return;
    NSDictionary *end = ternaryfile::snapshot(_read,_assetsRead);
    if (end && ![_editStart isEqual:end]) {
        [_undoStack addObject:@[_editStart, end]];
        if (_undoStack.count > 32) [_undoStack removeObjectAtIndex:0];
        [_redoStack removeAllObjects];
    }
    _editStart = nil;
    _undoButton.enabled = _undoStack.count > 0;
    _redoButton.enabled = _redoStack.count > 0;
}
-(void)remember {
    if (_editStart) return;
    _editStart = ternaryfile::snapshot(_read,_assetsRead);
    __weak TRNEditor *weak = self;
    dispatch_async(dispatch_get_main_queue(), ^{ [weak finishEdit]; });
}
-(void)editParam:(int)pid value:(float)value {
    [self remember];
    _write(pid,value);
    self.needsDisplay=YES;
}
-(BOOL)restore:(NSDictionary *)snapshot {
    if (!ternaryfile::restore(snapshot,_read,_write,_assetsRead,_assetsWrite)) {
        [self message:@"宿主未能载入工作流素材，请重新打开后重试。"]; return NO;
    }
    ++_importGeneration; _loadingKind=nil; _nextAssetRefresh=0;
    [_canvas refreshGraph];
    [self updateVisibleNodes];
    [self refresh];
    self.needsDisplay = YES;
    return YES;
}
-(void)history:(BOOL)undo {
    [self finishEdit];
    NSMutableArray *source = undo ? _undoStack : _redoStack;
    NSMutableArray *destination = undo ? _redoStack : _undoStack;
    NSArray *entry = source.lastObject;
    if (!entry) return;
    NSDictionary *was = entry[undo ? 1 : 0], *target = entry[undo ? 0 : 1];
    NSMutableDictionary *result = [ternaryfile::snapshot(_read,_assetsRead) mutableCopy];
    if (!result) { [self message:@"无法读取宿主素材，请重试。"]; return; }
    // Reverse only fields touched by the edit. Keep unrelated host automation.
    for (NSString *group in @[@"parameters", @"connections", @"nodes", @"assets"]) {
        NSMutableDictionary *merged = [result[group] mutableCopy];
        NSMutableSet *keys = [NSMutableSet setWithArray:[was[group] allKeys]];
        [keys addObjectsFromArray:[target[group] allKeys]];
        for (NSString *key in keys) {
            id a = was[group][key], b = target[group][key];
            if (![a isEqual:b]) {
                if (b) merged[key] = b;
                else [merged removeObjectForKey:key];
            }
        }
        result[group] = merged;
    }
    NSString *error = nil;
    if (!ternaryfile::validate(result,&error)) { [self message:error]; return; }
    if (![self restore:result]) return;
    [source removeLastObject];
    [destination addObject:entry];
    _undoButton.enabled = _undoStack.count > 0;
    _redoButton.enabled = _redoStack.count > 0;
}
-(void)undoWorkflow:(id)sender { [self history:YES]; }
-(void)redoWorkflow:(id)sender { [self history:NO]; }
-(void)message:(NSString *)message {
    NSAlert *alert = [NSAlert new]; alert.messageText = @"Soundnet 工作流";
    alert.informativeText = message ?: @"操作失败。";
    if (self.window) [alert beginSheetModalForWindow:self.window completionHandler:nil];
}
-(void)openWorkflow:(id)sender {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = @"打开 Soundnet 工作流";
    panel.allowsMultipleSelection = NO;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
        if (response != NSModalResponseOK) return;
        NSNumber *size = nil;
        if (![panel.URL getResourceValue:&size forKey:NSURLFileSizeKey error:nil] || size.unsignedLongLongValue>32ULL*1024*1024) {
            [self message:@"请选择不超过 32 MB 的 Soundnet 工作流 JSON。"]; return;
        }
        NSError *error = nil;
        NSData *data = [NSData dataWithContentsOfURL:panel.URL options:0 error:&error];
        id file = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:&error] : nil;
        NSString *why = nil;
        if (!file || !ternaryfile::validate(file,&why)) { [self message:why ?: error.localizedDescription]; return; }
        [self remember]; if (![self restore:file]) return; [self.keys note:-1];
        self.workflowName = panel.URL.lastPathComponent.stringByDeletingPathExtension;
        [self.patch selectItemAtIndex:0]; [self.canvas fitGraph];
    }];
}
-(void)saveWorkflow:(id)sender {
    [self.window makeFirstResponder:self];
    NSSavePanel *panel = [NSSavePanel savePanel];
    panel.title = @"保存 Soundnet 工作流";
    panel.nameFieldStringValue = [(_workflowName.length ? _workflowName : @"Soundnet 工作流") stringByAppendingPathExtension:@"json"];
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
        if (response != NSModalResponseOK) return;
        NSError *error = nil;
        NSDictionary *snapshot=ternaryfile::snapshot(self.read,self.assetsRead);
        if (!snapshot) { [self message:@"无法读取宿主素材，请重试。"]; return; }
        NSData *data = [NSJSONSerialization dataWithJSONObject:snapshot
             options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:&error];
        if (!data || ![data writeToURL:panel.URL options:NSDataWritingAtomic error:&error]) {
            [self message:error.localizedDescription]; return;
        }
        self.workflowName = panel.URL.lastPathComponent.stringByDeletingPathExtension;
        self.needsDisplay = YES;
    }];
}
-(void)templates:(NSMenuItem *)item {
    ++_importGeneration; _loadingKind=nil; _nextAssetRefresh=0;
    [self remember]; [self panic:nil];
    tri::Routing routes = tri::defaultRouting;
    int visible=tri::layout::kDefaultVisible, focus=0;
    if (item.tag == 2) { // Lower voice follows the keyboard, upper voices play the arp.
        routes[tri::RouteOsc1In-tri::RouteArpIn]=tri::MidiNode;
        _write(tri::ArpOn,1); _write(tri::Pattern,1); _write(tri::ArpRepeat,2);
    } else if (item.tag == 3) {
        routes.fill(0);
        routes[tri::RouteEQIn-tri::RouteArpIn]=tri::InputNode;
        routes[tri::RouteDelayIn-tri::RouteArpIn]=tri::EQNode;
        routes[tri::RouteOutputIn-tri::RouteArpIn]=tri::DelayNode;
    } else if (item.tag == 4) routes.fill(0);
    if(item.tag==5) {
        routes[tri::RouteVelocityIn-tri::RouteArpIn]=tri::ProgressNode;
        routes[tri::RouteArpIn-tri::RouteArpIn]=tri::VelocityNode;
        routes[tri::RouteArpClock-tri::RouteArpIn]=tri::ClockNode;
        routes[tri::RouteProgressClock-tri::RouteArpIn]=tri::ClockNode;
        routes[tri::RouteVelocityClock-tri::RouteArpIn]=tri::ClockNode;
        _write(tri::ArpOn,1);_write(tri::ProgressRun,0);
        visible=(1<<tri::VelocityNode)-1;
    } else if (item.tag==6 || item.tag==9) {
        routes.fill(0);
        routes[tri::routeIndex(tri::RouteModelIn)]=tri::ClipNode;
        routes[tri::routeIndex(tri::RouteClipClock)]=tri::ClockNode;
        visible=bit(tri::ClipNode)|bit(tri::ModelNode)|bit(tri::ClockNode);
        if (item.tag==9) {
            routes[tri::routeIndex(tri::RouteMidiOutIn)]=tri::ModelNode;
            visible|=bit(tri::MidiOutNode);
        } else {
            routes[tri::routeIndex(tri::RouteOsc1In)]=tri::ModelNode;
            routes[tri::routeIndex(tri::RouteOutputIn)]=tri::Osc1Node;
            visible|=bit(tri::Osc1Node)|bit(tri::OutputNode);
        }
        focus=tri::ClipNode;
        _write(tri::MidiLocalOnly,1); _write(tri::ClipRun,0); _write(tri::ClipLoop,1);
        _write(tri::ClockSync,_inputRead?0:1);
    } else if (item.tag==7) {
        routes[tri::routeIndex(tri::RouteArpIn)]=tri::DawMidiNode;
        visible=(visible & ~bit(tri::MidiNode))|bit(tri::DawMidiNode);
        focus=tri::DawMidiNode;
        _write(tri::MidiLocalOnly,1);
    } else if (item.tag==8) {
        routes.fill(0); visible=0;
        _write(tri::MidiLocalOnly,1); _write(tri::ClipRun,0);
    }
    _write(tri::layout::visibility,visible);
    _write(tri::layout::beginRouting,0);
    for(size_t i=0;i<routes.size();++i) _write(tri::routeID(i),routes[i]);
    _write(tri::layout::commitRouting,0);
    _workflowName=item.title;
    [_canvas resetLayout];
    if(item.tag==6 || item.tag==9) {
        const int nodes[]={tri::ClockNode,tri::ClipNode,tri::ModelNode,tri::Osc1Node,tri::OutputNode,tri::MidiOutNode};
        const int x[]={20,160,323,478,645,495};
        const int y[]={52,151,151,151,151,151};
        for(int i=0;i<6;++i) if (visible & bit(nodes[i])) {
            _write(tri::layout::x(nodes[i]),x[i]); _write(tri::layout::y(nodes[i]),y[i]);
        }
    }
    [_canvas refreshGraph]; [_canvas fitGraph]; [self updateVisibleNodes];
    if(focus) { [_canvas selectNode:focus]; [self inspect:focus]; }
    [self refresh]; self.needsDisplay=YES;
}
-(NSPopUpButton *)actionMenu:(NSString *)title rect:(NSRect)rect {
    NSPopUpButton *menu = [[NSPopUpButton alloc] initWithFrame:rect pullsDown:YES];
    [menu addItemWithTitle:title]; menu.font = font(12, true); menu.controlSize = NSControlSizeSmall;
    [self addSubview:menu]; return menu;
}
-(void)addAction:(NSPopUpButton *)menu title:(NSString *)title action:(SEL)action tag:(NSInteger)tag {
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:@""];
    item.target = self; item.tag = tag; [menu.menu addItem:item];
}
-(void)zoomIn:(id)sender { [_canvas zoomIn]; }
-(void)zoomOut:(id)sender { [_canvas zoomOut]; }
-(void)fit:(id)sender { [_canvas fitGraph]; }
-(void)tidy:(id)sender { [self remember]; [_canvas resetLayout]; [_canvas fitGraph]; }
-(void)chooseNode:(NSPopUpButton *)menu {
    if (menu.selectedItem.tag == envelopeInspector) [self selectPanel:nil];
    else { [_canvas selectNode:menu.selectedItem.tag]; [self inspect:menu.selectedItem.tag]; }
}
-(void)addModule:(NSMenuItem *)item { [_canvas addNode:item.tag]; [self inspect:item.tag]; [self updateVisibleNodes]; }
-(void)selectPanel:(id)sender { [_canvas selectNode:0]; [self inspect:envelopeInspector]; }
-(void)updateVisibleNodes {
    NSInteger mask = (NSInteger)_read(tri::layout::visibility);
    if (_visibility == mask) return;
    _visibility = mask;
    [_addMenu removeAllItems]; [_addMenu addItemWithTitle:@"节点库"];
    [_nodeMenu removeAllItems]; [_nodeMenu addItemWithTitle:@"画布"]; _nodeMenu.lastItem.tag=0;
    for (int n=1; n<=tri::layout::nodeCount; ++n) {
        BOOL present = mask & (1<<(n-1));
        NSString *name=n==tri::OutputNode ? (_inputRead ? @"硬件输出" : @"DAW 输出") : nodeTitles()[n];
        if (present) {
            [_nodeMenu addItemWithTitle:name]; _nodeMenu.lastItem.tag=n;
        }
        // The same library focuses existing nodes or adds disconnected modules.
        [self addAction:_addMenu title:present ? name : [@"+  " stringByAppendingString:name]
                 action:@selector(nodeLibrary:) tag:n];
        _addMenu.lastItem.state = present ? NSControlStateValueOn : NSControlStateValueOff;
    }
    [_nodeMenu addItemWithTitle:@"共享包络"]; _nodeMenu.lastItem.tag=envelopeInspector;
    if (_inspected>0 && _inspected<envelopeInspector && !(mask & (1<<(_inspected-1)))) [self inspect:0];
    [_nodeMenu selectItemWithTag:_inspected];
}
-(void)nodeLibrary:(NSMenuItem *)item {
    if ((int)_read(tri::layout::visibility) & (1<<(item.tag-1))) {
        [_canvas selectNode:item.tag]; [self inspect:item.tag];
    } else [self addModule:item];
}
-(void)caption:(NSString *)title y:(int)y {
    NSTextField *l=[NSTextField labelWithString:title];
    l.frame=NSMakeRect(side+14,y+3,119,20); l.font=font(12);
    l.textColor=color(foreground); [self addSubview:l]; [_inspectorViews addObject:l];
}
-(void)info:(NSString *)s y:(int)y {
    NSTextField *l=[NSTextField wrappingLabelWithString:s];
    l.frame=NSMakeRect(side+15,y,224,52); l.font=font(11); l.textColor=color(muted);
    [self addSubview:l]; [_inspectorViews addObject:l];
}
-(void)number:(int)id name:(NSString *)name y:(int)y {
    [self caption:name y:y]; [self addNumber:id rect:parameterRect(side+124,y,111)];
}
-(void)localMenu:(NSArray *)items id:(int)id name:(NSString *)name y:(int)y {
    [self caption:name y:y];
    [self popup:NSMakeRect(side+105,y,130,25) items:items id:id action:@selector(selectMenu:)];
}
-(void)connectFromMenu:(NSPopUpButton *)button {
    [self editParam:(int)button.tag value:(float)button.selectedItem.tag];
    [_canvas refreshGraph]; [self refresh];
}
-(void)route:(int)pid name:(NSString *)name y:(int)y {
    [self caption:name y:y];
    NSPopUpButton *menu=[self popup:NSMakeRect(side+102,y,133,25) items:@[] id:-1 action:@selector(connectFromMenu:)];
    menu.tag=pid; menu.identifier=[NSString stringWithFormat:@"route.%d",pid]; menu.autoenablesItems=NO;
    [menu setAccessibilityLabel:name];
    // Items are kept stable while the user browses the native menu.
    for(int n=0;n<=tri::layout::nodeCount;++n) {
        if (!tri::compatibleSource(pid,n)) continue;
        NSString *label=n ? codes()[n] : @"未连接";
        if (tri::clockPort(pid)) label=n ? @"节奏速度节点" : (_inputRead ? @"MIDI 时钟 / 备用" : @"DAW 速度");
        [menu addItemWithTitle:label]; menu.lastItem.tag=n;
    }
}
-(void)inspect:(NSInteger)n {
    if (n<0 || n>envelopeInspector) n=0;
    if (_inspected==n && _inspectorViews.count) return;
    if ([self.window.firstResponder isKindOfClass:NSTextView.class]) [self.window makeFirstResponder:self];
    _inspected=n;
    for(NSView *v in _inspectorViews) [v removeFromSuperview];
    [_inspectorViews removeAllObjects]; [_fields removeAllObjects]; [_steppers removeAllObjects]; [_menus removeAllObjects];
    _arpButton=nil; _inputButton=nil; _symbols=nil; _musicPlay=nil; _clipLoop=nil; _assetName=nil; _importButton=nil;
    [_nodeMenu selectItemWithTag:n];
    _buildingInspector=YES;
    if (n>=tri::Osc1Node && n<=tri::Osc3Node) {
        int voice=(int)n-tri::Osc1Node, osc=tri::Wave1+3*voice, env=tri::Osc1Attack+3*voice;
        [self route:tri::RouteOsc1In+voice name:@"音符来源" y:145];
        [self localMenu:@[@"正弦 Sine",@"方波 Square",@"锯齿 Saw",@"三角 Triangle"] id:osc name:@"波形" y:181];
        [self number:osc+1 name:@"电平  %" y:216]; [self number:osc+2 name:@"移调  st" y:251];
        [self number:env name:@"Attack  ms" y:301]; [self number:env+1 name:@"Sustain  %" y:336];
        [self number:env+2 name:@"Release  ms" y:371];
        [self info:@"↳ 跟随共享包络。输入数值单独设置，清空后按回车恢复跟随。" y:414];
        [self button:NSMakeRect(side+13,470,136,26) title:@"编辑共享包络…" action:@selector(selectPanel:)];
    } else if(n==tri::ArpNode) {
        [self route:tri::RouteArpIn name:@"音符来源" y:145];
        _arpButton=[NSButton checkboxWithTitle:@"启用琶音器" target:self action:@selector(toggleArp:)];
        _arpButton.identifier=@"arp.enable"; _arpButton.frame=NSMakeRect(side+13,182,200,23);
        [self addSubview:_arpButton]; [_inspectorViews addObject:_arpButton];
        [self localMenu:@[@"Straight 8ths",@"Bossa Nova",@"Reggae",@"Disco",@"Pulse 16ths"] id:tri::Pattern name:@"节奏型" y:218];
        [self localMenu:@[@"1×",@"2×",@"3×",@"4×",@"5×",@"6×",@"7×",@"8×"] id:tri::ArpRepeat name:@"每音重复" y:255];
        [self route:tri::RouteArpClock name:@"速度来源" y:292];
        [self number:tri::Gate name:@"门长  %" y:329];
        [self number:tri::Tempo name:@"备用速度  BPM" y:462];
        [self info:@"按住和弦。亮格触发一次音符，重复次数决定何时切换到下一音。" y:414];

    } else if(n==tri::ChordNode || n==tri::ProgressNode) {
        BOOL sequence=n==tri::ProgressNode;
        [self caption:sequence?@"预制走向":@"预制和弦" y:145];
        NSPopUpButton *preset=[self popup:NSMakeRect(side+101,145,134,25)
            items:sequence?@[@"选择模板…",@"流行 I–V–vi–IV",@"爵士 ii–V–I",@"Cmaj7–Am7–Dm7–G7",@"Blues 八和弦"]:@[@"选择模板…",@"C",@"Cm",@"C7",@"Cmaj7",@"Cm7",@"Csus2",@"Csus4"]
            id:-1 action:@selector(harmonyPreset:)];
        preset.tag=sequence?tri::ProgressNode:tri::ChordNode;
        [self info:sequence?@"自行输入，最多 8 个和弦，以 | 分隔。":@"自行输入和弦名称，如 Cmaj7 或 F#m7。" y:184];
        _symbols=[[NSTextField alloc]initWithFrame:NSMakeRect(side+14,220,224,25)];
        _symbols.font=font(11,false,true);_symbols.target=self;_symbols.action=@selector(symbolsChanged:);_symbols.delegate=self;
        _symbols.identifier=sequence?@"progression.symbols":@"chord.symbols";
        [self addSubview:_symbols];[_inspectorViews addObject:_symbols];
        _musicPlay=[NSButton checkboxWithTitle:sequence?@"循环演奏走向":@"演奏和弦" target:self action:@selector(playMusic:)];
        _musicPlay.frame=NSMakeRect(side+14,259,222,25);_musicPlay.tag=sequence?tri::ProgressRun:tri::ChordRun;
        [self addSubview:_musicPlay];[_inspectorViews addObject:_musicPlay];
        if(sequence) {
            [self number:tri::ProgressBars name:@"每和弦小节数" y:303];
            [self route:tri::RouteProgressClock name:@"速度来源" y:344];
        } else [self route:tri::RouteChordIn name:@"MIDI 根音" y:303];
        [self info:sequence?@"走向循环输出完整和弦，可接入 ARP、力度节点或振荡器。":@"接入 MIDI 时按所弹根音生成和弦。勾选演奏可直接输出输入的和弦。" y:400];
    } else if(n==tri::ClockNode) {
        [self number:tri::ClockBPM name:@"速度  BPM" y:145];
        [self localMenu:@[@"模块 BPM",_inputRead ? @"DAW MIDI 时钟" : @"DAW 工程速度"] id:tri::ClockSync name:@"时钟模式" y:192];
        [self info:_inputRead ? @"向 Soundnet MIDI In 发送 MIDI Clock 可同步外部速度。未收到时钟时使用上方 BPM。" : @"选择 DAW 工程速度时跟随 Logic 的速度变化。选择模块 BPM 后，此节点按上方速度运行。" y:242];
        [self info:@"将速度输出接到 MIDI Clip、ARP、和声走向或力度节点。" y:312];
    } else if(n==tri::VelocityNode) {
        [self route:tri::RouteVelocityIn name:@"音符来源" y:145];
        [self route:tri::RouteVelocityClock name:@"速度来源" y:183];
        [self number:tri::VelocityMin name:@"最小力度" y:225];
        [self number:tri::VelocityMax name:@"最大力度" y:261];
        [self number:tri::VelocityRate name:@"周期  拍" y:297];
        [self localMenu:@[@"正弦起伏",@"渐强循环",@"强弱交替"] id:tri::VelocityShape name:@"变化方式" y:333];
        [self info:@"在音符经过时按周期改变 MIDI 力度，输出继续接 ARP 或振荡器。" y:390];
    } else if(n==tri::ClipNode || n==tri::ModelNode) {
        BOOL clip=n==tri::ClipNode;
        [self route:clip?tri::RouteClipClock:tri::RouteModelIn name:clip?@"速度来源":@"音符来源" y:145];
        _assetName=[NSTextField wrappingLabelWithString:@""];
        _assetName.frame=NSMakeRect(side+15,194,222,46); _assetName.font=font(12);
        _assetName.identifier=@"asset.name"; _assetName.lineBreakMode=NSLineBreakByTruncatingMiddle;
        [self addSubview:_assetName]; [_inspectorViews addObject:_assetName];
        _nextAssetRefresh=0;
        _importButton=[self button:NSMakeRect(side+12,250,145,27) title:clip?@"载入 MIDI 文件…":@"载入音符模型…" action:@selector(importAsset:)];
        _importButton.identifier=clip?@"clip.import":@"model.import";
        if (clip) {
            _musicPlay=[self check:@"启用播放" param:tri::ClipRun y:299];
            _clipLoop=[self check:@"循环片段" param:tri::ClipLoop y:334];
            [self info:_inputRead ? @"使用模块 BPM 时，勾选播放从片段开头起奏。同步 DAW 时，播放位置由外部 MIDI 时钟和传输消息控制。" : @"使用 DAW 速度时，勾选播放并启动 Logic 走带。使用模块 BPM 时，勾选播放从片段开头起奏。" y:389];
        } else {
            [self info:@"载入 Core ML 单音高模型或 JSON 音高映射。模型会把收到的 MIDI 音符转换成新的音高，再送往下游节点。" y:305];
            [self info:@"未载入模型时，原音高直接通过。" y:398];
        }
    } else if(n==tri::InputNode) {
        [self number:tri::InputGain name:@"输入电平  %" y:145];
        if(_inputWrite) _inputButton=[self button:NSMakeRect(side+13,191,130,27) title:@"开启输入" action:@selector(toggleInput:)];
        [self info:_inputWrite?@"信号来自 Mac 的系统默认输入设备。":@"在 Logic 的插件窗口顶部选择 Side Chain 音源。" y:241];
    } else if(n==tri::MixNode) {
        for(int i=0;i<4;++i) [self route:tri::RouteMix1+i name:[NSString stringWithFormat:@"输入 %d",i+1] y:145+38*i];
        [self info:@"四路音频相加，输出为立体声。" y:323];
    } else if(n==tri::EQNode || n==tri::DelayNode) {
        BOOL eq=n==tri::EQNode;
        [self route:eq?tri::RouteEQIn:tri::RouteDelayIn name:@"音频来源" y:145];
        NSArray *labels=eq?@[@"低频 180 Hz · dB",@"中频 1 kHz · dB",@"高频 5 kHz · dB"]:@[@"时间  ms",@"反馈  %",@"干湿混合  %"];
        for(int i=0;i<3;++i) [self number:(eq?tri::EQLow:tri::DelayTime)+i name:labels[i] y:195+38*i];
    } else if(n==tri::MidiOutNode) {
        [self route:tri::RouteMidiOutIn name:@"音符来源" y:145];
        [self info:_inputRead ? @"此节点向虚拟端口 Soundnet MIDI Out 发送音符。在接收软件中选择这个 MIDI 输入端口，即可驱动其他乐器。" : @"音符通过 Audio Unit 的 MIDI 输出回调交给 DAW。宿主需要支持接收乐器插件生成的 MIDI。" y:205];
    } else if(n==tri::OutputNode) {
        [self route:tri::RouteOutputIn name:@"音频来源" y:145]; [self number:tri::Master name:@"总输出  %" y:195];
        [self info:_inputRead ? @"立体声输出送往 macOS 当前的默认输出设备。可在系统声音设置中选择声卡或扬声器。" : @"立体声输出返回当前 DAW 乐器轨道。在 Logic 的通道条中选择后续效果、总线或硬件输出。" y:251];
    } else if(n==envelopeInspector) {
        NSArray *labels=@[@"Attack  ms",@"Decay  ms",@"Sustain  %",@"Release  ms"];
        for(int i=0;i<4;++i) [self number:tri::Attack+i name:labels[i] y:145+38*i];
        [self info:@"三路振荡器共用 Decay。各路的 Attack、Sustain、Release 可在对应节点单独设置。" y:322];
    } else if(n==tri::MidiNode) {
        [self localMenu:@[@"兼容合并",@"仅本地输入"] id:tri::MidiLocalOnly name:@"接收方式" y:145];
        [self info:_inputRead ? @"本地输入包括电脑键盘、下方琴键和 CoreMIDI 键盘。DAW 可通过 Soundnet MIDI In 单独接入。" : @"本地输入来自下方琴键。用 DAW MIDI 输入节点接收 Logic 轨道的音符。" y:196];
        [self info:@"兼容合并会接收 DAW 音符。一旦 DAW MIDI 节点接出连线，两种来源会自动分开。" y:270];
    } else if(n==tri::DawMidiNode) {
        [self info:_inputRead ? @"在 DAW 的外部 MIDI 轨道中把目的地设为 Soundnet MIDI In。此节点会收到该端口的音符。" : @"接收当前 DAW 乐器轨道上的实时 MIDI 和区域音符。在 Logic 中将 Soundnet 载入软件乐器插槽。" y:148];
        [self info:@"把音符输出接到和弦、ARP、模型或振荡器。屏幕琴键可通过本地 MIDI 节点另行接线。" y:226];
    } else [self info:@"点选节点查看参数，或从节点库添加模块。" y:145];
    _buildingInspector=NO; [self refresh]; self.needsDisplay=YES;
}

-(NSString*)chordText:(int)root quality:(int)q {
    NSArray *roots=@[@"C",@"C#",@"D",@"Eb",@"E",@"F",@"F#",@"G",@"Ab",@"A",@"Bb",@"B"];
    NSArray *qualities=@[@"",@"m",@"7",@"maj7",@"m7",@"dim",@"sus2",@"sus4"];
    return [roots[(root%12+12)%12] stringByAppendingString:qualities[std::clamp(q,0,7)]];
}
-(NSString*)harmonyText:(BOOL)sequence {
    if(!sequence)return [self chordText:(int)_read(tri::ChordRoot) quality:(int)_read(tri::ChordQuality)];
    NSMutableArray *parts=[NSMutableArray new];
    for(int i=0;i<(int)_read(tri::ProgressLength);++i)
        [parts addObject:[self chordText:(int)_read(tri::ProgressRoot1+i*2) quality:(int)_read(tri::ProgressQuality1+i*2)]];
    return [parts componentsJoinedByString:@" | "];
}
-(void)setHarmony:(NSString*)string sequence:(BOOL)sequence {
    auto parsed=tri::parseProgression(string.UTF8String);
    if(!parsed || (!sequence && parsed->size()!=1)) {
        [self message:@"请输入 1–8 个有效和弦，例如 Cmaj7 | Am7 | Dm7 | G7。支持大、小、7、maj7、m7、dim、sus2、sus4。"];return;
    }
    [self remember];
    if(sequence) {
        [self editParam:tri::ProgressRun value:0];
        [self editParam:tri::ProgressLength value:parsed->size()];
        for(size_t i=0;i<parsed->size();++i) {
            [self editParam:tri::ProgressRoot1+(int)i*2 value:(*parsed)[i].root];
            [self editParam:tri::ProgressQuality1+(int)i*2 value:(*parsed)[i].quality];
        }
    } else {
        [self editParam:tri::ChordRoot value:parsed->front().root];
        [self editParam:tri::ChordQuality value:parsed->front().quality];
    }
    [self refresh];
}
-(void)symbolsChanged:(NSTextField*)field { [self setHarmony:field.stringValue sequence:_inspected==tri::ProgressNode]; }
-(void)harmonyPreset:(NSPopUpButton*)menu {
    if(menu.indexOfSelectedItem==0)return;
    NSArray *sequences=@[@"",@"C | G | Am | F",@"Dm7 | G7 | Cmaj7",@"Cmaj7 | Am7 | Dm7 | G7",@"C7 | F7 | C7 | C7 | F7 | F7 | C7 | G7"];
    [self setHarmony:menu.tag==tri::ProgressNode?sequences[menu.indexOfSelectedItem]:menu.titleOfSelectedItem sequence:menu.tag==tri::ProgressNode];
}
-(void)playMusic:(NSButton*)button { [self editParam:(int)button.tag value:button.state==NSControlStateValueOn?1:0]; }
-(NSButton *)check:(NSString *)title param:(int)pid y:(int)y {
    NSButton *button=[NSButton checkboxWithTitle:title target:self action:@selector(playMusic:)];
    button.tag=pid; button.frame=NSMakeRect(side+14,y,220,25);
    button.identifier=[NSString stringWithFormat:@"param.%d.check",pid];
    [self addSubview:button]; [_inspectorViews addObject:button]; return button;
}
-(void)importAsset:(id)sender {
    if (!_assetsRead || !_assetsWrite || _loadingKind) return;
    BOOL clip=_inspected==tri::ClipNode;
    NSString *kind=clip ? @"clip" : @"model";
    NSOpenPanel *panel=[NSOpenPanel openPanel];
    panel.title=clip ? @"载入 MIDI Clip" : @"载入音符模型";
    panel.allowsMultipleSelection=NO; panel.canChooseDirectories=!clip;
    NSMutableArray<UTType *> *types=[NSMutableArray new];
    for (NSString *ext in (clip ? @[@"mid",@"midi"] : @[@"mlmodel",@"mlmodelc",@"mlpackage",@"json"])) {
        UTType *type=[UTType typeWithFilenameExtension:ext];
        if (type) [types addObject:type];
    }
    panel.allowedContentTypes=types;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
        if(response==NSModalResponseOK) [self loadAssetURL:panel.URL kind:kind];
    }];
}
-(void)loadAssetURL:(NSURL *)url kind:(NSString *)kind {
    if (!_assetsRead || !_assetsWrite) return;
    NSUInteger generation=++_importGeneration;
    NSDictionary *initial=_assetsRead();
    if (!initial) { [self message:@"无法读取宿主素材，请重试。"]; return; }
    NSDictionary *original=initial[kind];
    _loadingKind=kind; _nextAssetRefresh=0; [self refresh];
    __weak TRNEditor *weak=self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        NSString *why=nil;
        NSDictionary *asset=triassets::importAsset(kind,url,&why);
        dispatch_async(dispatch_get_main_queue(), ^{
            TRNEditor *editor=weak;
            if(!editor || generation!=editor.importGeneration) return;
            editor.loadingKind=nil; editor.nextAssetRefresh=0;
            if (!asset) { [editor refresh]; [editor message:why ?: @"无法读取素材。"]; return; }
            // Importing Core ML can take time. Merge only this slot into the current host patch.
            NSDictionary *current=editor.assetsRead();
            if (!current || (current[kind]!=original && ![current[kind] isEqual:original])) {
                [editor refresh]; [editor message:@"载入期间宿主更改了素材，请重新选择文件。"]; return;
            }
            NSMutableDictionary *next=[current mutableCopy]; next[kind]=asset;
            [editor remember];
            if(!editor.assetsWrite(next)) [editor message:@"宿主未能载入素材。"];
            [editor refresh];
        });
    });
}
-(void)setup {
    self.appearance=[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    _fields=[NSMutableDictionary new]; _steppers=[NSMutableDictionary new]; _menus=[NSMutableArray new];
    _inspectorViews=[NSMutableArray new]; _undoStack=[NSMutableArray new]; _redoStack=[NSMutableArray new];
    _visibility=-1; _inspected=-1;
    _workflowMenu=[self actionMenu:@"工作流" rect:NSMakeRect(171,13,98,26)];
    [self addAction:_workflowMenu title:@"打开工作流 JSON…" action:@selector(openWorkflow:) tag:0];
    [self addAction:_workflowMenu title:@"保存工作流 JSON…" action:@selector(saveWorkflow:) tag:0];
    [_workflowMenu.menu addItem:NSMenuItem.separatorItem];
    NSArray *templates=@[@"标准合成线路", @"分层琶音", @"外部音频效果器", @"清空接线", @"和声编曲工作流", @"MIDI Clip → 模型 → 音频", @"DAW MIDI 演奏", @"空白工作流", @"MIDI Clip → 模型 → MIDI 输出"];
    for(int i=0;i<(int)templates.count;++i) [self addAction:_workflowMenu title:templates[i] action:@selector(templates:) tag:i+1];
    _patch=[self popup:NSMakeRect(328,13,155,25) items:@[@"当前设置",@"基础合成 / Init",@"玻璃琴键 / Glass",@"圆润贝斯 / Sub",@"缓起铺底 / Haze"]
                     id:-1 action:@selector(applyPatch:)];
    [_patch setAccessibilityLabel:@"音色"];
    _addMenu=[self actionMenu:@"节点库" rect:NSMakeRect(12,48,95,25)];
    _undoButton=[self button:NSMakeRect(115,48,56,25) title:@"撤销" action:@selector(undoWorkflow:)];
    _redoButton=[self button:NSMakeRect(172,48,56,25) title:@"重做" action:@selector(redoWorkflow:)];
    _undoButton.enabled=NO; _redoButton.enabled=NO;
    [self button:NSMakeRect(648,48,34,25) title:@"−" action:@selector(zoomOut:)];
    [self button:NSMakeRect(684,48,34,25) title:@"+" action:@selector(zoomIn:)];
    [self button:NSMakeRect(721,48,66,25) title:@"适配" action:@selector(fit:)];
    [self button:NSMakeRect(788,48,77,25) title:@"整理布局" action:@selector(tidy:)];
    __weak TRNEditor *weak=self;
    _canvas=[[TRIWorkflowCanvas alloc] initWithFrame:NSMakeRect(12,78,853,463) read:_read
        write:^(int pid,float v){ [weak editParam:pid value:v]; }
        selected:^(int n){ [weak inspect:n]; }];
    _canvas.hosted = !_inputRead;
    _canvas.identifier=@"workflow.canvas"; [self addSubview:_canvas];
    _nodeMenu=[self popup:NSMakeRect(side+12,98,225,28) items:@[] id:-1 action:@selector(chooseNode:)];
    _nodeMenu.identifier=@"workflow.inspector"; [_nodeMenu setAccessibilityLabel:@"选择节点参数"];
    _keys=[[TRNKeys alloc] initWithFrame:NSMakeRect(145,574,984,52)]; _keys.send=_send; _keys.held=-1;
    [self addSubview:_keys];
    [self button:NSMakeRect(17,597,112,26) title:@"停音  Panic" action:@selector(panic:)];
    [self updateVisibleNodes];
    int first=tri::Osc1Node;
    if (!(_visibility&(1<<(first-1)))) {
        first=0;
        for(int n=1;n<=tri::layout::nodeCount && !first;++n) if(_visibility&(1<<(n-1))) first=n;
    }
    [_canvas selectNode:first]; [self inspect:first]; [_canvas fitGraph];
    [self refresh];
}
-(void)refresh {
    if (!_canvas) return;
    for (NSNumber *key in _fields) [self syncField:key.intValue forced:NO];
    for (NSPopUpButton *popup in _menus) {
        int pid=(int)popup.tag;
        int i=(int)std::lround(_read(pid))-(pid==tri::ArpRepeat?1:0);
        if(i!=popup.indexOfSelectedItem && i>=0 && i<popup.numberOfItems) {
            [popup selectItemAtIndex:i]; self.needsDisplay=YES;
        }
    }
    if(_symbols && !_symbols.currentEditor) _symbols.stringValue=[self harmonyText:_inspected==tri::ProgressNode];
    if(_musicPlay) _musicPlay.state=_read((int)_musicPlay.tag)>.5?NSControlStateValueOn:NSControlStateValueOff;
    if(_clipLoop) _clipLoop.state=_read(tri::ClipLoop)>.5?NSControlStateValueOn:NSControlStateValueOff;
    if(_assetName && NSProcessInfo.processInfo.systemUptime >= _nextAssetRefresh) {
        _nextAssetRefresh=NSProcessInfo.processInfo.systemUptime+1.;
        BOOL clip=_inspected==tri::ClipNode;
        NSString *kind=clip?@"clip":@"model";
        NSDictionary *assets=_assetsRead ? _assetsRead() : nil;
        NSDictionary *asset=assets[kind];
        NSString *name=asset[@"name"];
        if ([_loadingKind isEqual:kind]) name=@"正在载入…";
        else if (clip && name && asset[@"beats"] && asset[@"notes"])
            name=[NSString stringWithFormat:@"%@\n%@ 个音符 · %@ 拍",name, asset[@"notes"],numberText([asset[@"beats"] doubleValue])];
        _assetName.stringValue=name ?: (clip ? @"未载入 MIDI Clip" : @"未载入模型");
        _assetName.toolTip=_assetName.stringValue;
        _importButton.enabled=_assetsRead && _assetsWrite && !_loadingKind;
        if (clip) _musicPlay.enabled=asset!=nil;
    }
    _arpButton.state=_read(tri::ArpOn)>.5?NSControlStateValueOn:NSControlStateValueOff;
    if(_inputButton) _inputButton.title=_inputRead && _inputRead()?@"关闭输入":@"开启输入";
    [self updateVisibleNodes];
    tri::Routing routes{};
    for(size_t i=0;i<routes.size();++i) routes[i]=(int)std::lround(_read(tri::routeID(i)));
    for(NSView *view in _inspectorViews) {
        if(![view.identifier hasPrefix:@"route."]) continue;
        NSPopUpButton *menu=(NSPopUpButton*)view;
        const int p=(int)menu.tag;
        for(NSMenuItem *option in menu.itemArray) {
            int src=(int)option.tag;
            option.enabled=(!src || (_visibility&(1<<(src-1)))) && tri::canConnect(routes,p,src);
        }
        [menu selectItemWithTag:routes[tri::routeIndex(p)]];
    }
    [_canvas refreshGraph];
    NSString *status=_status?_status():@"";
    if(![_statusText isEqualToString:status]) { _statusText=status; self.needsDisplay=YES; }
}
-(void)viewDidMoveToWindow {
    [super viewDidMoveToWindow]; [_timer invalidate]; _timer=nil;
    if(self.window) {
        __weak TRNEditor *weak=self;
        _timer=[NSTimer scheduledTimerWithTimeInterval:.18 repeats:YES block:^(NSTimer*){[weak refresh];}];
    }
}
-(void)dealloc { [_timer invalidate]; }
-(void)drawRect:(NSRect)dirty {
    fill(self.bounds,background);
    text(@"Soundnet",NSMakeRect(15,6,130,34),24,foreground,true);
    text(@"1.4",NSMakeRect(127,18,38,18),11,muted,false,true);
    text(@"音色",NSMakeRect(289,16,42,22),12,muted);
    text(_workflowName?:@"",NSMakeRect(509,16,370,19),12,muted);
    text(_statusText?:@"",NSMakeRect(912,17,216,18),11,muted,false,true);
    fill(NSMakeRect(12,42,1120,1),border);
    fill(NSMakeRect(259,58,12,3),0xe7b87b); text(@"音符",NSMakeRect(278,51,56,18),11,muted);
    fill(NSMakeRect(331,58,12,3),0x82c3b2); text(@"音频",NSMakeRect(350,51,56,18),11,muted);
    fill(NSMakeRect(402,58,12,3),0xb2a5e0); text(@"速度",NSMakeRect(421,51,56,18),11,muted);
    box(NSMakeRect(side,48,255,493));
    heading(@"节点参数",side+13,61);
    text(_inspected>0 && _inspected<envelopeInspector?codes()[_inspected]:@"",NSMakeRect(side+154,66,86,17),10,muted,true,true);
    fill(NSMakeRect(side+1,133,253,1),border);
    if (_inspected==tri::ArpNode) {
        int pattern=std::clamp((int)_read(tri::Pattern),0,4);
        for(int i=0;i<16;++i) fill(NSMakeRect(side+14+i*14,384,11,10),
            tri::patternMasks[pattern]&(1<<i)?active:0x434849);
    }
    int source=std::clamp((int)_read(tri::RouteOutputIn),0,tri::layout::nodeCount);
    NSString *sink=_inputRead ? @"硬件输出" : @"DAW 输出";
    NSString *monitor=source ? [NSString stringWithFormat:@"%@   →   %@",codes()[source],sink] : [sink stringByAppendingString:@"未连接"];
    int midiSource=std::clamp((int)_read(tri::RouteMidiOutIn),0,tri::layout::nodeCount);
    if (midiSource) {
        NSString *midi=[NSString stringWithFormat:@"%@   →   MIDI 输出",codes()[midiSource]];
        monitor=source ? [NSString stringWithFormat:@"%@     ·     %@",monitor,midi] : midi;
    }
    text(monitor,NSMakeRect(14,545,775,18),11,source||midiSource?muted:0xe7b87b);
    fill(NSMakeRect(12,566,1120,1),border);
    text(@"演奏键盘",NSMakeRect(18,576,112,19),13,foreground,true);
    text(@"C3 – B5",NSMakeRect(147,631,150,16),10,muted,false,true);
}
@end

NSView *TRICreateEditor(TRIRead read, TRIWrite write, TRIMIDI midi,
                        TRIInputRead inputRead, TRIInputWrite inputWrite, TRIStatus status,
                        TRIAssetsRead assetsRead, TRIAssetsWrite assetsWrite) {
    TRNEditor *editor=[[TRNEditor alloc] initWithFrame:NSMakeRect(0,0,width,height)];
    editor.read=read; editor.write=write; editor.send=midi;
    editor.assetsRead=assetsRead; editor.assetsWrite=assetsWrite;
    editor.inputRead=inputRead; editor.inputWrite=inputWrite; editor.status=status;
    [editor setup]; return editor;
}
