#import "TriEditor.h"
#import "SNLocalization.h"
#include <atomic>
#include <memory>
#import "WorkflowFile.hpp"
#import "../src/WorkflowAssets.h"
#import "../src/OfflineRender.hpp"
#import "../src/GenBackend.hpp"
#import "../src/AbcScore.hpp"
#import "../src/WavFile.hpp"
#import "../src/MidiFileWriter.hpp"
#include "../src/StandardMidiFile.hpp"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <cmath>

namespace {
NSArray *catalog() {
    NSMutableArray *rows=[NSMutableArray new];
    for(int type:tri::nodeCatalog()) {
        const tri::NodeMeta *meta=tri::nodeMeta(type);
        if(meta)[rows addObject:@[@(type),SNLocalized(@(meta->title)),SNLocalized(@(meta->category))]];
    }
    return rows;
}
NSString *title(int type) { return SNLocalized(@(tri::nodeTitle(type))); }
// v4 documents identify a node by `nodeKey` and a parameter by its stable key.
// The helpers below are the only place that knows about the wire shape, so an
// older numeric document still opens and is rewritten canonically on save.
int typeOf(NSDictionary *node) {
    id key=node[@"nodeKey"];
    if([key isKindOfClass:NSString.class]) {
        const int resolved=tri::nodeTypeForKey([key UTF8String]);
        if(resolved)return resolved;
    }
    return [node[@"type"] intValue];
}
NSString *paramKeyName(int pid) {
    return [NSString stringWithUTF8String:tri::paramKey(pid)];
}
NSNumber *paramValue(NSDictionary *node,int pid) {
    NSDictionary *parameters=node[@"parameters"];
    id value=parameters[paramKeyName(pid)];
    if(!value)value=parameters[[@(pid) stringValue]];
    return value ?: @(tri::graphParamInfo(pid).def);
}
void setParam(NSMutableDictionary *node,int pid,double value) {
    NSMutableDictionary *parameters=node[@"parameters"];
    if(![parameters isKindOfClass:NSMutableDictionary.class]) {
        parameters=[NSMutableDictionary new];
        node[@"parameters"]=parameters;
    }
    parameters[paramKeyName(pid)]=@(value);
    [parameters removeObjectForKey:[@(pid) stringValue]];
}
int inputPortOf(NSDictionary *node,NSDictionary *edge) {
    id port=edge[@"port"];
    if([port isKindOfClass:NSString.class])return tri::nodePortIndex(typeOf(node),true,[port UTF8String]);
    return port ? [port intValue] : 0;
}
int outputPortOf(NSDictionary *node,NSDictionary *edge) {
    id port=edge[@"sourcePort"];
    if([port isKindOfClass:NSString.class])return tri::nodePortIndex(typeOf(node),false,[port UTF8String]);
    return port ? [port intValue] : 0;
}
NSMutableDictionary *makeEdge(uint64_t source,uint64_t target,int targetType,int targetPort,int sourceType,int sourcePort) {
    NSMutableDictionary *edge=[NSMutableDictionary new];
    edge[@"source"]=@(source);edge[@"target"]=@(target);
    const char* targetId=tri::nodePortId(targetType,true,size_t(targetPort));
    const char* sourceId=tri::nodePortId(sourceType,false,size_t(sourcePort));
    if(targetId&&*targetId)edge[@"port"]=[NSString stringWithUTF8String:targetId];
    if(sourceId&&*sourceId)edge[@"sourcePort"]=[NSString stringWithUTF8String:sourceId];
    return edge;
}
static NSString *nodeLabel(NSDictionary *node) {
    NSString *name=node[@"name"];
    if([name isKindOfClass:NSString.class] && name.length)return name;
    // A node written by a newer build keeps its place on the canvas.
    if(typeOf(node)==0)return SNLocalized(@"缺失节点");
    return title(typeOf(node));
}
static NSArray<NSString*> *paramOptions(int p) {
    // Enumerations live in the descriptor: one list for validation, storage and
    // the inspector.
    const tri::ParamSpec *spec=tri::paramSpec(p);
    if(!spec||!spec->options||!spec->optionCount)return nil;
    NSMutableArray *rows=[NSMutableArray arrayWithCapacity:spec->optionCount];
    for(int i=0;i<spec->optionCount;++i)
        rows[i]=SNLocalized([NSString stringWithUTF8String:spec->options[i]]);
    return rows;
}
NSArray<NSString *> *voiceNames() { return @[SNLocalized(@"高声部"), SNLocalized(@"中声部"), SNLocalized(@"低声部")]; }
struct VoiceControls { int lower,upper,texture; };
constexpr VoiceControls voiceControls[]={
    {tri::ArrHighMin,tri::ArrHighMax,tri::ArrHighTexture},
    {tri::ArrMidMin,tri::ArrMidMax,tri::ArrMidTexture},
    {tri::ArrLowMin,tri::ArrLowMax,tri::ArrLowTexture}
};
NSNumber *voiceValue(NSDictionary *node,int pid) {
    return paramValue(node,pid);
}
NSString *pitchName(int note) {
    static NSString *const names[]={@"C",@"C♯",@"D",@"E♭",@"E",@"F",@"F♯",@"G",@"A♭",@"A",@"B♭",@"B"};
    return [NSString stringWithFormat:@"%@%d",names[note%12],note/12-1];
}
// Port names come from the descriptor, so a node that renames or adds a port
// never needs an editor change. The signal is only a fallback for unknown types.
NSString *portLabel(int type,bool input,int port) {
    const tri::PortSpec *spec=tri::nodePort(type,input,size_t(port));
    if(spec&&spec->label&&*spec->label)return SNLocalized(@(spec->label));
    if(spec) {
        if(spec->signal==tri::Signal::Audio)return SNLocalized(@"音频");
        if(spec->signal==tri::Signal::Midi)return SNLocalized(@"音符");
        if(spec->signal==tri::Signal::Trigger)return SNLocalized(@"触发");
        if(spec->signal==tri::Signal::Clock)return SNLocalized(@"时钟");
    }
    return input?SNLocalized(@"输入"):SNLocalized(@"输出");
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
std::vector<int> controls(int type) { return tri::nodeParamList(type); }
NSString *parameterName(int p) {
    switch(p) {
        case tri::TriggerCount:return SNLocalized(@"收到 X 次后触发");
        case tri::WaitSeconds:return SNLocalized(@"等待秒数");
        case tri::ClockBPM:return @"BPM";
        case tri::ClipRun:return SNLocalized(@"播放  0 / 1");
        case tri::ClipLoop:return SNLocalized(@"循环  0 / 1");
        default:return SNParameterTitle(@(tri::graphParamInfo(p).name));
    }
}
}

namespace { NSRect nodeRect(NSDictionary *node); }

static NSString *const SNNodeTypePasteboard = @"com.soundnet.node-type";

@class SNInstanceEditor;
@interface SNInstanceCanvas : NSView
@property(weak) SNInstanceEditor *editor;
@end

@interface SNWaveformView : NSView
@property(nonatomic,strong) NSData *pcm;
@property(nonatomic) NSInteger channels;
@end
@implementation SNWaveformView
- (void)drawRect:(NSRect)dirty {
    NSRect bounds=NSInsetRect(self.bounds,1,1);
    [[NSColor colorWithWhite:.11 alpha:1] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:bounds xRadius:5 yRadius:5] fill];
    [[NSColor colorWithWhite:.28 alpha:1] setStroke];
    NSBezierPath *frame=[NSBezierPath bezierPathWithRoundedRect:bounds xRadius:5 yRadius:5];
    [frame stroke];
    if(!_pcm.length || _channels<1)return;
    NSUInteger count=_pcm.length/sizeof(float),frames=count/NSUInteger(_channels);
    if(!frames)return;
    const float *samples=(const float *)_pcm.bytes;
    CGFloat middle=NSMidY(self.bounds),half=NSHeight(self.bounds)*.42;
    NSUInteger width=NSUInteger(NSWidth(self.bounds));
    if(!width)return;
    [[NSColor colorWithCalibratedRed:.28 green:.78 blue:.94 alpha:1] setStroke];
    NSBezierPath *wave=[NSBezierPath bezierPath];
    for(NSUInteger x=0;x<width;++x) {
        NSUInteger a=frames*x/width,b=frames*(x+1)/width;
        if(b<=a)b=a+1;if(b>frames)b=frames;
        float lo=0,hi=0;
        for(NSUInteger f=a;f<b;++f) {
            float value=0;
            for(NSInteger c=0;c<_channels;++c)value+=samples[f*NSUInteger(_channels)+NSUInteger(c)];
            value/=float(_channels);
            lo=std::min(lo,value);hi=std::max(hi,value);
        }
        lo=std::max(-1.f,std::min(1.f,lo));hi=std::max(-1.f,std::min(1.f,hi));
        [wave moveToPoint:NSMakePoint(CGFloat(x)+.5,middle-hi*half)];
        [wave lineToPoint:NSMakePoint(CGFloat(x)+.5,middle-lo*half)];
    }
    [wave setLineWidth:1];[wave stroke];
    [[NSColor colorWithWhite:.45 alpha:.7] setStroke];
    NSBezierPath *center=[NSBezierPath bezierPath];
    [center moveToPoint:NSMakePoint(NSMinX(bounds),middle)];
    [center lineToPoint:NSMakePoint(NSMaxX(bounds),middle)];
    [center setLineWidth:.5];[center stroke];
}
@end

static double snapBeat(double value, double grid=0.25) { return std::round(value/grid)*grid; }

static NSString *autosaveFilePath() {
    NSString *base=[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES) firstObject];
    if(!base.length)return nil;
    NSString *directory=[base stringByAppendingPathComponent:@"Soundnet"];
    [[NSFileManager defaultManager]createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
    return [directory stringByAppendingPathComponent:@"Autosave.json"];
}

@interface SNNoteRollView : NSView
@property(nonatomic,strong) NSMutableArray<NSMutableDictionary *> *notes;
@property(nonatomic) double lengthBeats;
@property(nonatomic) double grid;
@property(nonatomic) NSInteger selectedIndex;
@property(copy) void (^selectionChanged)(void);
- (void)deleteSelected;
@end

@implementation SNNoteRollView {
    BOOL _dragging, _resizing;
    NSMutableDictionary *_noteClipboard;
    double _grid;
    NSPoint _dragPoint;
    double _dragStartBeat, _dragLength;
    int _dragStartPitch;
}
- (BOOL)isFlipped { return YES; }
- (double)grid { return _grid>0?_grid:.25; }
- (void)setGrid:(double)grid { _grid=grid>0?grid:.25; [self setNeedsDisplay:YES]; }
- (void)setLengthBeats:(double)lengthBeats {
    _lengthBeats=std::max(.5,std::min(64.0,lengthBeats));
    [self setNeedsDisplay:YES];
}
- (double)left { return 48; }
- (double)top { return 24; }
- (double)rowHeight { return 12; }
- (double)beatWidth { return std::max(6.0,(NSWidth(self.bounds)-[self left]-8)/std::max(.5,_lengthBeats)); }
- (int)pitchAt:(NSPoint)p {
    int pitch=84-int(std::floor((p.y-[self top])/[self rowHeight]));
    return std::max(48,std::min(84,pitch));
}
- (double)beatAt:(NSPoint)p { return (p.x-[self left])/[self beatWidth]; }
- (NSRect)rectForNote:(NSDictionary *)note {
    double start=[note[@"start"] doubleValue],length=[note[@"length"] doubleValue];
    int pitch=[note[@"pitch"] intValue];
    return NSMakeRect([self left]+start*[self beatWidth],
                      [self top]+(84-pitch)*[self rowHeight]+1,
                      std::max(5.0,length*[self beatWidth]),[self rowHeight]-2);
}
- (NSInteger)noteAt:(NSPoint)p {
    for(NSInteger i=(NSInteger)_notes.count-1;i>=0;--i)
        if(NSPointInRect(p,[self rectForNote:_notes[(NSUInteger)i]]))return i;
    return -1;
}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    [[NSColor colorWithWhite:.10 alpha:1] setFill];NSRectFill(self.bounds);
    double bw=[self beatWidth],left=[self left],top=[self top],row=[self rowHeight];
    static bool black[12]={false,true,false,true,false,false,true,false,true,false,true,false};
    for(int pitch=48;pitch<=84;++pitch) {
        double y=top+(84-pitch)*row;
        if(black[pitch%12]){[[NSColor colorWithWhite:.135 alpha:1] setFill];NSRectFill(NSMakeRect(left,y,NSWidth(self.bounds)-left-4,row));}
        [[NSColor colorWithWhite:.20 alpha:1] setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(left,y+.5) toPoint:NSMakePoint(NSMaxX(self.bounds)-4,y+.5)];
        if(pitch%12==0){NSString *name=pitchName(pitch);NSDictionary *attrs=@{NSFontAttributeName:[NSFont monospacedSystemFontOfSize:9 weight:NSFontWeightRegular],NSForegroundColorAttributeName:[NSColor colorWithWhite:.65 alpha:1]};[name drawAtPoint:NSMakePoint(4,y+1) withAttributes:attrs];}
    }
    [[NSColor colorWithWhite:.16 alpha:1] setFill];NSRectFill(NSMakeRect(0,0,left,NSHeight(self.bounds)));
    for(double beat=0;beat<=_lengthBeats+1e-6;beat+=[self grid]) {
        double x=left+beat*bw;
        bool whole=std::fabs(beat-std::round(beat))<1e-6;
        [[NSColor colorWithWhite:whole?.30:.20 alpha:1] setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(x,top) toPoint:NSMakePoint(x,NSHeight(self.bounds)-2)];
        if(whole){NSString *label=[NSString stringWithFormat:@"%.0f",beat];[label drawAtPoint:NSMakePoint(x+3,4) withAttributes:@{NSFontAttributeName:[NSFont monospacedSystemFontOfSize:9 weight:NSFontWeightRegular],NSForegroundColorAttributeName:[NSColor colorWithWhite:.6 alpha:1]}];}
    }
    for(NSUInteger i=0;i<_notes.count;++i) {
        NSRect rect=[self rectForNote:_notes[i]];
        bool selected=NSInteger(i)==_selectedIndex;
        [[NSColor colorWithCalibratedRed:.92 green:.62 blue:.30 alpha:.92] setFill];
        [[NSBezierPath bezierPathWithRoundedRect:rect xRadius:3 yRadius:3] fill];
        [[NSColor colorWithWhite:selected?1:.55 alpha:selected?1:.7] setStroke];
        NSBezierPath *border=[NSBezierPath bezierPathWithRoundedRect:rect xRadius:3 yRadius:3];
        [border setLineWidth:selected?2:1];[border stroke];
    }
    [[NSColor colorWithWhite:.28 alpha:1] setStroke];
    NSBezierPath *frame=[NSBezierPath bezierPathWithRect:NSInsetRect(self.bounds,.5,.5)];
    [frame stroke];
}
- (void)mouseDown:(NSEvent *)event {
    NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];
    NSInteger hit=[self noteAt:p];
    if(hit>=0) {
        _selectedIndex=hit;
        if(_selectionChanged)_selectionChanged();
        NSRect rect=[self rectForNote:_notes[(NSUInteger)hit]];
        _dragging=YES;_resizing=p.x>=NSMaxX(rect)-6;
        _dragPoint=p;_dragStartBeat=[_notes[(NSUInteger)hit][@"start"] doubleValue];
        _dragLength=[_notes[(NSUInteger)hit][@"length"] doubleValue];
        _dragStartPitch=[_notes[(NSUInteger)hit][@"pitch"] intValue];
    } else if(p.x>=[self left] && p.y>=[self top]) {
        double beat=std::max(0.0,std::min(_lengthBeats-.25,snapBeat([self beatAt:p],[self grid])));
        if(beat>=_lengthBeats)return;
        NSMutableDictionary *note=[@{@"pitch":@([self pitchAt:p]),@"start":@(beat),
            @"length":@(std::min(1.0,std::max(.25,_lengthBeats-beat))),@"velocity":@100} mutableCopy];
        [_notes addObject:note];_selectedIndex=(NSInteger)_notes.count-1;
        if(_selectionChanged)_selectionChanged();
    }
    [self setNeedsDisplay:YES];
    [self.window makeFirstResponder:self];
}
- (void)mouseDragged:(NSEvent *)event {
    if(!_dragging || _selectedIndex<0 || _selectedIndex>=(NSInteger)_notes.count)return;
    NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];
    NSMutableDictionary *note=_notes[(NSUInteger)_selectedIndex];
    double beat=[self beatAt:p];
    if(_resizing) {
        double start=[note[@"start"] doubleValue];
        note[@"length"]=@(std::max([self grid],std::min(_lengthBeats-start,snapBeat(beat-start,[self grid]))));
    } else {
        double start=std::max(0.0,std::min(_lengthBeats-[self grid],snapBeat(_dragStartBeat+beat-[self beatAt:_dragPoint],[self grid])));
        int pitch=std::max(48,std::min(84,_dragStartPitch-int(std::round((p.y-_dragPoint.y)/[self rowHeight]))));
        note[@"start"]=@(start);note[@"pitch"]=@(pitch);
    }
    [self setNeedsDisplay:YES];
}
- (void)mouseUp:(NSEvent *)event { (void)event;_dragging=NO;_resizing=NO; }
- (void)rightMouseDown:(NSEvent *)event {
    NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];
    NSInteger hit=[self noteAt:p];
    if(hit>=0){[_notes removeObjectAtIndex:(NSUInteger)hit];_selectedIndex=-1;if(_selectionChanged)_selectionChanged();[self setNeedsDisplay:YES];}
}
- (BOOL)acceptsFirstResponder { return YES; }
- (void)copySelectedNote {
    if(_selectedIndex<0 || _selectedIndex>=(NSInteger)_notes.count)return;
    _noteClipboard=[_notes[(NSUInteger)_selectedIndex] mutableCopy];
}
- (void)pasteNote {
    if(!_noteClipboard)return;
    NSMutableDictionary *note=[_noteClipboard mutableCopy];
    double start=std::min(_lengthBeats-[self grid],
        std::max(0.0,snapBeat([note[@"start"] doubleValue]+[self grid],[self grid])));
    note[@"start"]=@(start);
    note[@"length"]=@(std::max([self grid],std::min(_lengthBeats-start,[note[@"length"] doubleValue])));
    [_notes addObject:note];_selectedIndex=(NSInteger)_notes.count-1;
    if(_selectionChanged)_selectionChanged();
    [self setNeedsDisplay:YES];
}
- (void)keyDown:(NSEvent *)event {
    NSString *chars=event.charactersIgnoringModifiers.lowercaseString;
    if((event.modifierFlags&NSEventModifierFlagCommand) && [chars isEqualToString:@"c"]) {[self copySelectedNote];return;}
    if((event.modifierFlags&NSEventModifierFlagCommand) && [chars isEqualToString:@"v"]) {[self pasteNote];return;}
    if(event.keyCode==51 || event.keyCode==117)[self deleteSelected];
    else [super keyDown:event];
}
- (void)deleteSelected {
    if(_selectedIndex<0 || _selectedIndex>=(NSInteger)_notes.count)return;
    [_notes removeObjectAtIndex:(NSUInteger)_selectedIndex];
    _selectedIndex=-1;if(_selectionChanged)_selectionChanged();
    [self setNeedsDisplay:YES];
}
@end

@interface SNInstanceEditor : NSView <NSTextFieldDelegate> {
    // Generation runs on a worker thread; the UI thread only flips the cancel
    // flag, so the backend object is never touched from two threads at once.
    std::shared_ptr<std::atomic<bool>> aiCancellation;
    std::shared_ptr<std::atomic<int>> aiProgressShared;
    // Set by the "regenerate from score" button; consumed by -generateAudio:.
    BOOL aiUseScore;
}
@property(copy) TRIRead read;
@property(copy) TRIWrite write;
@property(copy) TRIMIDI midi;
@property(copy) TRINodeStop nodeStop;
@property(copy) TRIInputRead inputRead;
@property(copy) TRIInputWrite inputWrite;
@property(copy) TRIAssetsRead assetsRead;
@property(copy) TRIAssetsWrite assetsWrite;
@property(copy) TRIProcessingRead processingRead;
@property(copy) TRIProcessingWrite processingWrite;
@property BOOL aiBusy;
@property uint64_t aiNode;
@property(strong) NSString *aiStatusText;
@property(strong) NSTextField *aiStatusField;
@property(strong) NSProgressIndicator *aiProgressView;
@property BOOL processingActive;
@property BOOL libraryVisible;
@property(strong) NSArray<NSButton *> *fileButtons;
@property(strong) NSArray<NSButton *> *editButtons;
@property(strong) NSButton *playButton;
@property(strong) NSButton *stopButton;
@property(strong) NSButton *libraryToggle;
@property(strong) NSPopUpButton *languagePopup;
@property(strong) NSTextField *processingLabel;
@property(strong) NSButton *cancelButton;
@property(strong) NSProgressIndicator *exportProgress;
@property BOOL exportBusy;
@property std::shared_ptr<std::atomic<bool>> exportCancellation;
@property(strong) NSMutableSet<NSString *> *collapsedParameterGroups;
@property uint64_t inspectedUID;
@property uint64_t auditionGeneration;
@property(strong) NSMutableSet<NSNumber *> *auditionNotes;
@property(strong) NSMutableDictionary *document;
@property(strong) NSMutableArray *history;
@property(strong) NSMutableArray *future;
@property(strong) SNInstanceCanvas *canvas;
@property(strong) NSScrollView *libraryScroll;
@property(strong) NSView *library;
@property(strong) NSScrollView *inspectorScroll;
@property(strong) NSView *inspector;
@property(strong) NSTextField *status;
@property(strong) NSTextField *heading;
@property(copy) NSString *headingText;
@property(strong) NSButton *keyboardToggle;
@property(strong) NSTextField *zoomLabel;
@property(strong) NSArray<NSView *> *zoomControls;
@property BOOL keyboardVisible;
@property(strong) NSView *piano;
@property(strong) NSDictionary *dragStartGroup;
@property uint64_t groupDrag;
@property NSPoint groupGrab;
@property(strong) NSMutableDictionary<NSNumber *,NSArray *> *groupDragNodes;
@property uint64_t selected;
@property(strong) NSMutableSet<NSNumber *> *selection;
@property NSInteger selectedEdge;
@property(strong) NSDictionary *clipboard;
@property BOOL snap;
@property BOOL boxSelecting;
@property NSPoint boxOrigin;
@property NSPoint boxCurrent;
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
@property(strong) dispatch_queue_t autosaveQueue;
@property(strong) NSPanel *clipPanel;
@property(strong) SNNoteRollView *clipRoll;
@property uint64_t clipUID;
@property(strong) NSTextField *clipLengthField;
@property(strong) NSTextField *clipNoteLengthField;
@property(strong) NSTextField *clipVelocityField;
-(void)editClip:(id)sender;
-(void)clipApply:(id)sender;
-(void)clipCancel:(id)sender;
-(void)clipDelete:(id)sender;
-(void)clipLengthChanged:(id)sender;
-(void)clipNoteLengthChanged:(id)sender;
-(void)clipVelocityChanged:(id)sender;
-(void)clipGridChanged:(id)sender;
-(void)clipQuantize:(id)sender;
-(void)clipPreview:(id)sender;
-(void)clipAudition:(id)sender;
-(void)refreshClipFields;
-(void)autosave;
-(void)flushAutosave;
-(void)restoreAutosave:(id)sender;
-(NSMutableDictionary *)node:(uint64_t)uid;
-(BOOL)isSelected:(uint64_t)uid;
-(NSArray<NSNumber *> *)selectedIDs;
-(NSArray<NSNumber *> *)activeIDs;
-(void)selectOnly:(uint64_t)uid;
-(void)toggleSelection:(uint64_t)uid;
-(void)copy:(id)sender;
-(void)paste:(id)sender;
-(void)cut:(id)sender;
-(void)selectAll:(id)sender;
-(void)selectNone:(id)sender;
-(void)deleteEdge:(NSInteger)index;
-(void)pruneSelection;
-(void)snap:(id)sender;
-(void)search:(id)sender;
-(void)inspect;
-(void)generateAudio:(id)sender;
-(void)generateFromScore:(id)sender;
-(void)importScore:(id)sender;
-(void)aiStop:(id)sender;
-(void)textChanged:(NSTextField *)field;
-(void)refreshAiStatus;
-(void)commit:(NSDictionary *)previous;
-(void)insert:(NSMenuItem *)sender;
-(BOOL)dropFileURLs:(NSArray<NSURL *> *)urls atPoint:(NSPoint)point clipMode:(BOOL)clipMode;
-(NSPoint)world:(NSPoint)point;
-(void)layoutUI;
-(void)toggleKeyboard:(id)sender;
-(void)zoomIn:(id)sender;
-(void)zoomOut:(id)sender;
-(void)zoomReset:(id)sender;
-(void)zoomBy:(double)factor atViewPoint:(NSPoint)point;
-(uint64_t)insertNodeType:(int)type atPoint:(NSPoint)point;
-(void)insertFromLibrary:(int)type;
-(NSMutableArray *)groups;
-(NSArray *)groupList;
-(void)groupSelection:(id)sender;
-(void)dissolveGroup:(id)sender;
-(void)renameGroup:(NSDictionary *)group;
-(void)renameGroupMenu:(NSMenuItem *)sender;
-(NSDictionary *)groupAtPoint:(NSPoint)point titleOnly:(BOOL)titleOnly;
-(void)commitGroups:(NSDictionary *)previous;
@end


namespace {
struct ParamPreset { const char *name; std::vector<std::pair<int,float>> values; };
std::vector<ParamPreset> presetsFor(int type) {
    switch(type) {
        case tri::FilterNode: return {
            {"低通 800 Hz",{{tri::FilterType,0},{tri::FilterCutoff,800},{tri::FilterResonance,.7f},{tri::FilterDrive,0}}},
            {"高通 180 Hz",{{tri::FilterType,1},{tri::FilterCutoff,180},{tri::FilterResonance,.7f},{tri::FilterDrive,0}}},
            {"带通 1.2 kHz",{{tri::FilterType,2},{tri::FilterCutoff,1200},{tri::FilterResonance,1.2f},{tri::FilterDrive,0}}} };
        case tri::ReverbNode: return {
            {"房间",{{tri::ReverbMix,.25f},{tri::ReverbSize,.55f},{tri::ReverbDamping,.5f},{tri::ReverbWidth,.8f}}},
            {"大厅",{{tri::ReverbMix,.45f},{tri::ReverbSize,.9f},{tri::ReverbDamping,.3f},{tri::ReverbWidth,1}}},
            {"近场预延迟",{{tri::ReverbMix,.3f},{tri::ReverbSize,.4f},{tri::ReverbDamping,.5f},{tri::ReverbWidth,.9f},{tri::ReverbPreDelay,15}}} };
        case tri::DelayNode: return {
            {"短延迟",{{tri::DelayTime,140},{tri::DelayFeedback,.2f},{tri::DelayMix,.15f}}},
            {"长回声",{{tri::DelayTime,420},{tri::DelayFeedback,.45f},{tri::DelayMix,.25f}}} };
        case tri::CompressorNode: return {
            {"轻压缩",{{tri::CompThreshold,-18},{tri::CompRatio,2},{tri::CompAttack,.01f},{tri::CompRelease,.15f},{tri::CompMakeup,1.1f},{tri::CompMix,1}}},
            {"鼓总线",{{tri::CompThreshold,-24},{tri::CompRatio,4},{tri::CompAttack,.005f},{tri::CompRelease,.12f},{tri::CompMakeup,1.4f},{tri::CompMix,1}}},
            {"限制",{{tri::CompThreshold,-6},{tri::CompRatio,20},{tri::CompAttack,.001f},{tri::CompRelease,.08f},{tri::CompMakeup,1},{tri::CompMix,1}}} };
        case tri::LimiterNode: return {
            {"轻柔",{{tri::LimiterThreshold,-6},{tri::LimiterRelease,.12f},{tri::LimiterCeiling,-.3f},{tri::LimiterGain,1}}},
            {"砖墙",{{tri::LimiterThreshold,-3},{tri::LimiterRelease,.05f},{tri::LimiterCeiling,-.1f},{tri::LimiterGain,1}}} };
        case tri::ChorusNode: return {
            {"轻微",{{tri::ChorusRate,.8f},{tri::ChorusDepth,.35f},{tri::ChorusMix,.3f},{tri::ChorusDelay,12}}},
            {"宽阔",{{tri::ChorusRate,2.5f},{tri::ChorusDepth,.6f},{tri::ChorusMix,.45f},{tri::ChorusDelay,20},{tri::ChorusFeedback,.2f}}},
            {"反馈空间",{{tri::ChorusRate,.9f},{tri::ChorusDepth,.5f},{tri::ChorusMix,.5f},{tri::ChorusDelay,18},{tri::ChorusFeedback,.55f}}} };
        case tri::TremoloNode: return {
            {"慢速",{{tri::TremoloRate,2},{tri::TremoloDepth,.4f},{tri::TremoloMix,.8f},{tri::TremoloStereo,0}}},
            {"快速",{{tri::TremoloRate,8},{tri::TremoloDepth,.6f},{tri::TremoloMix,1},{tri::TremoloStereo,1}}},
            {"同步 1/8",{{tri::TremoloSync,1},{tri::TremoloDivision,1},{tri::TremoloDepth,.6f},{tri::TremoloMix,1}}} };
        case tri::BitcrusherNode: return {
            {"8-bit",{{tri::CrushBits,8},{tri::CrushRate,12000},{tri::CrushMix,.8f}}},
            {"破碎",{{tri::CrushBits,4},{tri::CrushRate,4000},{tri::CrushMix,1}}} };
        case tri::PhaserNode: return {
            {"慢速扫频",{{tri::PhaserRate,.3f},{tri::PhaserDepth,.8f},{tri::PhaserFeedback,.35f},{tri::PhaserMix,.5f}}},
            {"喷气",{{tri::PhaserRate,1.5f},{tri::PhaserDepth,.6f},{tri::PhaserFeedback,.6f},{tri::PhaserMix,.6f}}} };
        case tri::AudioClipNode: return {
            {"平滑循环",{{tri::AudioClipLoop,1},{tri::AudioClipXfade,20},{tri::AudioClipFade,.005f}}},
            {"反向片段",{{tri::AudioClipLoop,1},{tri::AudioClipReverse,1},{tri::AudioClipXfade,20}}} };
        case tri::SamplerNode: return {
            {"平滑循环",{{tri::SamplerLoop,1},{tri::SamplerXfade,20},{tri::SamplerAttack,.005f},{tri::SamplerRelease,.12f}}} };
        case tri::QuantizeNode: return {
            {"C 大调",{{tri::QuantizeRoot,0},{tri::QuantizeScale,0},{tri::QuantizeMode,0}}},
            {"A 小调",{{tri::QuantizeRoot,9},{tri::QuantizeScale,1},{tri::QuantizeMode,0}}},
            {"C 五声",{{tri::QuantizeRoot,0},{tri::QuantizeScale,3},{tri::QuantizeMode,0}}} };
        case tri::EchoNode: return {
            {"短回声",{{tri::EchoTime,.18f},{tri::EchoRepeats,2},{tri::EchoFeedback,.4f},{tri::EchoPitch,0}}},
            {"八度回声",{{tri::EchoTime,.25f},{tri::EchoRepeats,3},{tri::EchoFeedback,.5f},{tri::EchoPitch,12}}} };
        case tri::GateNode: return {
            {"轻门",{{tri::GateThreshold,-50},{tri::GateAttack,.002f},{tri::GateRelease,.25f},{tri::GateRange,.6f}}},
            {"硬门",{{tri::GateThreshold,-24},{tri::GateAttack,.001f},{tri::GateRelease,.05f},{tri::GateRange,1}}} };
        case tri::TransposeNode: return {
            {"升八度",{{tri::TransposeSemitones,12}}},
            {"降八度",{{tri::TransposeSemitones,-12}}} };
        case tri::MetronomeNode: return {
            {"慢速",{{tri::MetronomeBPM,80},{tri::MetronomeLevel,.4f},{tri::MetronomeAccent,1},{tri::MetronomeBeats,4}}},
            {"快速",{{tri::MetronomeBPM,160},{tri::MetronomeLevel,.3f},{tri::MetronomeAccent,1},{tri::MetronomeBeats,4}}} };
        case tri::RandomNode: return {
            {"半数",{{tri::RandomChance,.5f}}},
            {"稀疏",{{tri::RandomChance,.2f}}} };
        default: return {};
    }
}
}
@implementation SNInstanceEditor
-(void)drawRect:(NSRect)dirty {
    [[NSColor colorWithCalibratedWhite:.105 alpha:1] setFill];NSRectFill(self.bounds);
    [[NSColor colorWithCalibratedWhite:.14 alpha:1] setFill];NSRectFill(NSMakeRect(0,0,NSWidth(self.bounds),92));
    [NSColor.separatorColor setFill];NSRectFill(NSMakeRect(12,91,NSWidth(self.bounds)-24,1));
}
-(BOOL)isFlipped{return YES;}
-(void)dealloc {[_timer invalidate];[[NSNotificationCenter defaultCenter] removeObserver:self];if(_exportCancellation)_exportCancellation->store(true);}
-(NSMutableDictionary *)node:(uint64_t)uid {
    for(NSMutableDictionary *node in _document[@"nodes"])if([node[@"id"] unsignedLongLongValue]==uid)return node;
    return nil;
}
-(BOOL)isSelected:(uint64_t)uid { return [_selection containsObject:@(uid)]; }
-(NSArray<NSNumber *> *)selectedIDs {
    return [_selection.allObjects sortedArrayUsingComparator:^NSComparisonResult(NSNumber *a,NSNumber *b){
        unsigned long long x=a.unsignedLongLongValue,y=b.unsignedLongLongValue;
        return x<y?NSOrderedAscending:x>y?NSOrderedDescending:NSOrderedSame;
    }];
}
-(void)selectOnly:(uint64_t)uid {
    [_selection removeAllObjects];
    if(uid)[_selection addObject:@(uid)];
    _selected=uid;_selectedEdge=-1;[self inspect];
}
-(void)toggleSelection:(uint64_t)uid {
    NSNumber *key=@(uid);
    if([_selection containsObject:key]) {
        [_selection removeObject:key];
        if(_selected==uid)_selected=_selection.count?[[self selectedIDs].lastObject unsignedLongLongValue]:0;
    } else {
        [_selection addObject:key];_selected=uid;
    }
    _selectedEdge=-1;[self inspect];
}
-(void)message:(NSString *)text {_status.stringValue=SNLocalizedDiagnostic(text ?: @"");}
-(void)commit:(NSDictionary *)previous {
    tri::GraphSpec graph;NSString *why=nil;
    std::vector<tri::Diagnostic> diagnostics;
    if(!triassets::decodeGraph(_document,graph,&why,false,true,&diagnostics) ||
       !_assetsWrite || !_assetsWrite(@{@"graph":_document})) {
        _document=copyJSON(previous) ?: [@{@"version":@4,@"projectId":@"local",@"nodes":[NSMutableArray new],@"edges":[NSMutableArray new]}mutableCopy];[self message:why ?: SNLocalized(@"宿主无法应用节点图。")];
    } else {
        if(previous && ![previous isEqual:_document]) {
            [_history addObject:copyJSON(previous)];[_future removeAllObjects];
            while(_history.count>64)[_history removeObjectAtIndex:0];
        }
        [self autosave];
        NSString *status=[NSString stringWithFormat:SNLocalized(@"%lu 个独立节点 · %lu 条连接"),
                          (unsigned long)[_document[@"nodes"] count],(unsigned long)[_document[@"edges"] count]];
        if(!diagnostics.empty()) {
            size_t fatal=0;
            for(const auto& diagnostic:diagnostics) if(diagnostic.fatal)++fatal;
            status=[status stringByAppendingFormat:SNLocalized(@" · %lu 处提示：%@"),(unsigned long)diagnostics.size(),
                    SNLocalizedDiagnostic([NSString stringWithUTF8String:diagnostics.front().message.c_str()])];
            if(fatal)_status.textColor=NSColor.systemOrangeColor;
            else _status.textColor=NSColor.secondaryLabelColor;
        }
        [self message:status];
    }
    [self pruneSelection];
    _canvas.needsDisplay=YES;
}
-(void)autosave {
    if(getenv("SOUNDNET_NO_AUTOSAVE"))return;
    // Coalesce rapid edits and keep JSON serialization/file I/O off the main thread.
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(flushAutosave) object:nil];
    [self performSelector:@selector(flushAutosave) withObject:nil afterDelay:0.75];
}
-(void)flushAutosave {
    if(getenv("SOUNDNET_NO_AUTOSAVE"))return;
    NSString *path=autosaveFilePath();if(!path.length)return;
    if(!_autosaveQueue)_autosaveQueue=dispatch_queue_create("com.soundnet.autosave",DISPATCH_QUEUE_SERIAL);
    NSDictionary *snapshot=copyJSON(_document);
    dispatch_async(_autosaveQueue, ^{
        NSData *data=[NSJSONSerialization dataWithJSONObject:snapshot options:0 error:nil];
        if(data)[data writeToFile:path atomically:YES];
    });
}
-(void)restoreAutosave:(id)sender {
    NSString *path=autosaveFilePath();
    NSData *data=path.length?[NSData dataWithContentsOfFile:path]:nil;
    if(!data){[self message:SNLocalized(@"没有可恢复的自动保存")];return;}
    id object=[NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:nil];
    tri::GraphSpec graph;NSString *why=nil;
    NSDictionary *canonical=[object isKindOfClass:NSDictionary.class]?triassets::canonicalGraph(object,&why):nil;
    if(!canonical || !triassets::decodeGraph(canonical,graph,&why,true,true)) {
        [self message:why ?: SNLocalized(@"自动保存文件无效")];return;
    }
    NSDictionary *previous=copyJSON(_document);
    _document=copyJSON(canonical);
    [_selection removeAllObjects];_selected=0;_selectedEdge=-1;
    [self commit:previous];[self inspect];[self fit:nil];
    [self message:SNLocalized(@"已恢复自动保存")];
}
-(void)pruneSelection {
    if(!_selection)_selection=[NSMutableSet new];
    NSMutableSet *alive=[NSMutableSet new];
    for(NSDictionary *node in _document[@"nodes"])if([_selection containsObject:node[@"id"]])[alive addObject:node[@"id"]];
    _selection=alive;
    if(_selected && [self node:_selected] && ![_selection containsObject:@(_selected)])[_selection addObject:@(_selected)];
    if(_selected && ![_selection containsObject:@(_selected)])
        _selected=_selection.count?[[self selectedIDs].lastObject unsignedLongLongValue]:0;
    if(_selectedEdge>=(NSInteger)[(NSArray *)_document[@"edges"] count])_selectedEdge=-1;
}
-(NSButton *)button:(NSString *)text frame:(NSRect)frame action:(SEL)action parent:(NSView *)parent {
    NSButton *b=[NSButton buttonWithTitle:SNLocalized(text) target:self action:action];
    b.font=[NSFont systemFontOfSize:12 weight:NSFontWeightMedium];b.bezelStyle=NSBezelStyleRounded;
    b.frame=frame;[parent addSubview:b];return b;
}
-(NSMutableDictionary *)makeNode:(int)type at:(NSPoint)point id:(uint64_t)uid {
    const tri::NodeSpec *spec=tri::nodeSpec(type);
    NSMutableDictionary *parameters=[NSMutableDictionary new];
    for(int p:controls(type))parameters[paramKeyName(p)]=@(tri::graphParamInfo(p).def);
    if(type==tri::Osc1Node)parameters[paramKeyName(tri::Level1)]=@.66;
    NSMutableDictionary *node=[@{@"id":@(uid),
        @"nodeKey":[NSString stringWithUTF8String:spec?spec->key:"soundnet.comment"],
        @"nodeVersion":@(spec?spec->nodeVersion:1),
        @"stateVersion":@(spec?spec->stateVersion:1),
        @"x":@(point.x),@"y":@(point.y),@"parameters":parameters} mutableCopy];
    [_document[@"nodes"] addObject:node];
    return node;
}
-(void)insert:(NSMenuItem *)sender {
    NSDictionary *previous=copyJSON(_document);
    uint64_t next=1;
    for(NSDictionary *node in _document[@"nodes"])next=std::max(next,[node[@"id"] unsignedLongLongValue]+1);
    [self makeNode:(int)sender.tag at:_menuPoint id:next];
    _selected=next;
    [_selection removeAllObjects];[_selection addObject:@(next)];
    [self commit:previous];[self inspect];
}
-(BOOL)dropFileURLs:(NSArray<NSURL *> *)urls atPoint:(NSPoint)point clipMode:(BOOL)clipMode {
    if(!urls.count)return NO;
    if(_exportBusy){[self message:SNLocalized(@"已有处理正在进行，请等待或取消。")];return NO;}
    NSMutableArray<NSDictionary *> *jobs=[NSMutableArray new];
    NSMutableArray<NSString *> *errors=[NSMutableArray new];
    NSSet *audioExtensions=[NSSet setWithArray:@[@"wav",@"aif",@"aiff",@"aifc",@"mp3",@"m4a",@"aac",@"caf",@"flac"]];
    for(NSURL *url in urls) {
        NSString *extension=url.pathExtension.lowercaseString;
        NSString *kind=nil; int type=0;
        if([extension isEqualToString:@"mid"]||[extension isEqualToString:@"midi"]) {kind=@"clip";type=tri::ClipNode;}
        else if([audioExtensions containsObject:extension]) {
            kind=@"audio";type=clipMode?tri::AudioClipNode:tri::SamplerNode;
        } else if([extension isEqualToString:@"mlmodel"]||[extension isEqualToString:@"mlpackage"]||[extension isEqualToString:@"mlmodelc"]) {
            kind=@"model";type=tri::ModelNode;
        } else {
            [errors addObject:[NSString stringWithFormat:SNLocalized(@"%@：不支持的格式"),url.lastPathComponent]];
            continue;
        }
        [jobs addObject:@{@"url":url,@"kind":kind,@"type":@(type)}];
    }
    if(!jobs.count) {
        if(errors.count)[self message:errors.firstObject];
        NSBeep();return NO;
    }
    if(![self beginExport])return NO;
    auto cancelled=_exportCancellation;
    NSDictionary *targetDocument=_document;
    [self message:SNLocalized(@"正在载入素材…")];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        @autoreleasepool {
            NSMutableArray<NSDictionary *> *imported=[NSMutableArray new];
            for(NSDictionary *job in jobs) {
                if(cancelled->load())break;
                NSString *why=nil;NSURL *url=job[@"url"];
                NSDictionary *asset=triassets::importAsset(job[@"kind"],url,&why);
                if(cancelled->load())break;
                if(asset)[imported addObject:@{@"asset":asset,@"type":job[@"type"]}];
                else [errors addObject:[NSString stringWithFormat:@"%@：%@",url.lastPathComponent,
                    SNLocalizedDiagnostic(why ?: SNLocalized(@"无法读取"))]];
            }
            dispatch_async(dispatch_get_main_queue(),^{
                if(cancelled->load()){[self finishExport:SNLocalized(@"已取消素材载入。")];return;}
                // Opening, restoring or undoing to another document invalidates
                // this drop. Edits in the same document remain intact.
                if(self.document!=targetDocument){[self finishExport:SNLocalized(@"节点已改变，未应用素材。")];return;}
                if(!imported.count){[self finishExport:errors.firstObject ?: SNLocalized(@"无法读取")];return;}
                NSDictionary *previous=copyJSON(self.document);
                uint64_t next=1,last=0;NSUInteger offset=0;
                for(NSDictionary *node in self.document[@"nodes"])next=std::max(next,[node[@"id"] unsignedLongLongValue]+1);
                for(NSDictionary *result in imported) {
                    const uint64_t uid=next++;int type=[result[@"type"] intValue];
                    NSMutableDictionary *node=[self makeNode:type at:NSMakePoint(point.x+offset*30,point.y+offset*30) id:uid];
                    node[@"asset"]=result[@"asset"];
                    if(type==tri::ClipNode){setParam(node,tri::ClipRun,1);setParam(node,tri::ClipLoop,1);}
                    else if(type==tri::AudioClipNode)setParam(node,tri::AudioClipLoop,1);
                    last=uid;++offset;
                }
                self.selected=last;[self.selection removeAllObjects];[self.selection addObject:@(last)];
                [self commit:previous];[self inspect];
                if(self.document!=targetDocument){[self finishExport:self.status.stringValue];return;}
                if(errors.count)[self finishExport:[NSString stringWithFormat:SNLocalized(@"已添加 %lu 个节点；%@"),(unsigned long)offset,errors.firstObject]];
                else [self finishExport:[NSString stringWithFormat:SNLocalized(@"已拖入 %lu 个文件"),(unsigned long)offset]];
            });
        }
    });
    // Drag acceptance is synchronous; decoding and the atomic UI commit are not.
    return YES;
}
-(NSArray<NSNumber *> *)activeIDs {
    if(_selection.count)return [self selectedIDs];
    return _selected?@[@(_selected)]:@[];
}
-(void)remove:(id)sender {
    NSArray<NSNumber *> *ids=[self activeIDs];if(!ids.count)return;
    NSDictionary *previous=copyJSON(_document);
    NSSet *removed=[NSSet setWithArray:ids];
    NSMutableArray *remaining=[NSMutableArray array];
    for(NSDictionary *node in _document[@"nodes"])if(![removed containsObject:node[@"id"]])[remaining addObject:node];
    _document[@"nodes"]=remaining;
    NSIndexSet *indices=[_document[@"edges"] indexesOfObjectsPassingTest:^BOOL(NSDictionary *edge,NSUInteger,BOOL*){
        return [removed containsObject:edge[@"source"]] || [removed containsObject:edge[@"target"]];
    }];
    [_document[@"edges"] removeObjectsAtIndexes:indices];
    [_selection removeAllObjects];_selected=0;_selectedEdge=-1;
    [self commit:previous];[self inspect];
}
-(void)disconnect:(id)sender {
    NSArray<NSNumber *> *ids=[self activeIDs];if(!ids.count)return;
    NSDictionary *previous=copyJSON(_document);
    NSSet *removed=[NSSet setWithArray:ids];
    NSIndexSet *indices=[_document[@"edges"] indexesOfObjectsPassingTest:^BOOL(NSDictionary *edge,NSUInteger,BOOL*){
        return [removed containsObject:edge[@"source"]] || [removed containsObject:edge[@"target"]];
    }];
    [_document[@"edges"] removeObjectsAtIndexes:indices];_selectedEdge=-1;
    [self commit:previous];
}
-(void)duplicate:(id)sender {
    NSArray<NSNumber *> *ids=[self activeIDs];if(!ids.count)return;
    NSDictionary *previous=copyJSON(_document);
    uint64_t next=1;
    for(NSDictionary *node in _document[@"nodes"])next=std::max(next,[node[@"id"] unsignedLongLongValue]+1);
    NSMutableDictionary<NSNumber *,NSNumber *> *mapping=[NSMutableDictionary new];
    NSMutableArray *copies=[NSMutableArray new];
    for(NSNumber *key in ids) {
        NSDictionary *original=[self node:key.unsignedLongLongValue];if(!original)continue;
        NSMutableDictionary *copy=copyJSON(original);
        copy[@"id"]=@(next++);mapping[key]=copy[@"id"];
        copy[@"x"]=@([copy[@"x"] doubleValue]+35);copy[@"y"]=@([copy[@"y"] doubleValue]+45);
        [copies addObject:copy];
    }
    if(!copies.count)return;
    NSMutableArray *edges=[NSMutableArray new];
    for(NSDictionary *edge in _document[@"edges"]) {
        NSNumber *source=mapping[edge[@"source"]],*target=mapping[edge[@"target"]];
        if(!source || !target)continue;
        NSMutableDictionary *copy=copyJSON(edge);copy[@"source"]=source;copy[@"target"]=target;
        [edges addObject:copy];
    }
    [_document[@"nodes"] addObjectsFromArray:copies];
    [_document[@"edges"] addObjectsFromArray:edges];
    [_selection removeAllObjects];
    for(NSDictionary *copy in copies){[_selection addObject:copy[@"id"]];_selected=[copy[@"id"] unsignedLongLongValue];}
    _selectedEdge=-1;[self commit:previous];[self inspect];
}
-(void)copy:(id)sender {
    NSArray<NSNumber *> *ids=[self activeIDs];if(!ids.count){[self message:SNLocalized(@"没有可复制的节点。")];return;}
    NSSet *wanted=[NSSet setWithArray:ids];
    NSMutableArray *nodes=[NSMutableArray new],*edges=[NSMutableArray new];
    for(NSNumber *key in ids) { NSDictionary *node=[self node:key.unsignedLongLongValue]; if(node)[nodes addObject:copyJSON(node)]; }
    for(NSDictionary *edge in _document[@"edges"])
        if([wanted containsObject:edge[@"source"]] && [wanted containsObject:edge[@"target"]])[edges addObject:copyJSON(edge)];
    _clipboard=@{@"nodes":nodes,@"edges":edges};
    [self message:[NSString stringWithFormat:SNLocalized(@"已复制 %lu 个节点。"),(unsigned long)nodes.count]];
}
-(void)cut:(id)sender {
    NSArray<NSNumber *> *ids=[self activeIDs];if(!ids.count)return;
    [self copy:nil];[self remove:nil];[self message:[NSString stringWithFormat:SNLocalized(@"已剪切 %lu 个节点。"),(unsigned long)ids.count]];
}
-(void)paste:(id)sender {
    NSDictionary *source=_clipboard;
    if(!source){[self message:SNLocalized(@"剪贴板为空。")];return;}
    NSDictionary *previous=copyJSON(_document);
    uint64_t next=1;
    for(NSDictionary *node in _document[@"nodes"])next=std::max(next,[node[@"id"] unsignedLongLongValue]+1);
    NSMutableDictionary<NSNumber *,NSNumber *> *mapping=[NSMutableDictionary new];
    NSMutableArray *copies=[NSMutableArray new];
    for(NSDictionary *node in source[@"nodes"]) {
        if(![node isKindOfClass:[NSDictionary class]] || !node[@"id"])continue;
        NSMutableDictionary *copy=copyJSON(node);
        copy[@"id"]=@(next++);mapping[node[@"id"]]=copy[@"id"];
        copy[@"x"]=@([copy[@"x"] doubleValue]+35);copy[@"y"]=@([copy[@"y"] doubleValue]+45);
        [copies addObject:copy];
    }
    if(!copies.count)return;
    NSMutableArray *edges=[NSMutableArray new];
    for(NSDictionary *edge in source[@"edges"]) {
        NSNumber *a=mapping[edge[@"source"]],*b=mapping[edge[@"target"]];
        if(!a || !b)continue;
        NSMutableDictionary *copy=copyJSON(edge);copy[@"source"]=a;copy[@"target"]=b;
        [edges addObject:copy];
    }
    [_document[@"nodes"] addObjectsFromArray:copies];
    [_document[@"edges"] addObjectsFromArray:edges];
    [_selection removeAllObjects];
    for(NSDictionary *copy in copies){[_selection addObject:copy[@"id"]];_selected=[copy[@"id"] unsignedLongLongValue];}
    _selectedEdge=-1;[self commit:previous];[self inspect];
    [self message:[NSString stringWithFormat:SNLocalized(@"已粘贴 %lu 个节点。"),(unsigned long)copies.count]];
}
-(void)selectAll:(id)sender {
    [_selection removeAllObjects];_selected=0;
    for(NSDictionary *node in _document[@"nodes"]){[_selection addObject:node[@"id"]];_selected=[node[@"id"] unsignedLongLongValue];}
    _selectedEdge=-1;[self inspect];_canvas.needsDisplay=YES;
}
-(void)selectNone:(id)sender {
    [_selection removeAllObjects];_selected=0;_selectedEdge=-1;[self inspect];_canvas.needsDisplay=YES;
}
-(void)deleteEdge:(NSInteger)index {
    if(index<0 || index>=(NSInteger)[(NSArray *)_document[@"edges"] count])return;
    NSDictionary *previous=copyJSON(_document);
    [_document[@"edges"] removeObjectAtIndex:index];_selectedEdge=-1;
    [self commit:previous];
}
-(void)snap:(id)sender {
    _snap=!_snap;
    if([sender isKindOfClass:[NSButton class]])((NSButton *)sender).state=_snap?NSControlStateValueOn:NSControlStateValueOff;
    [self message:_snap?SNLocalized(@"吸附已开启（20 点网格）。"):SNLocalized(@"吸附已关闭。")];
}
-(void)search:(id)sender {
    NSAlert *alert=[NSAlert new];alert.messageText=SNLocalized(@"查找节点");alert.informativeText=SNLocalized(@"输入节点名称或编号。");
    [alert addButtonWithTitle:SNLocalized(@"查找")];[alert addButtonWithTitle:SNLocalized(@"取消")];
    NSTextField *field=[[NSTextField alloc]initWithFrame:NSMakeRect(0,0,240,24)];
    field.placeholderString=SNLocalized(@"例如 滤波器、混响、27");alert.accessoryView=field;
    if([alert runModal]!=NSAlertFirstButtonReturn)return;
    NSString *needle=[field.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet].lowercaseString;
    if(!needle.length)return;
    NSDictionary *found=nil;
    for(NSDictionary *node in _document[@"nodes"]) {
        NSString *labelText=[[NSString stringWithFormat:@"%@ %@",nodeLabel(node),node[@"id"]] lowercaseString];
        if([[node[@"id"] stringValue] isEqualToString:needle] || [labelText containsString:needle]){found=node;break;}
    }
    if(!found){[self message:SNLocalized(@"没有找到匹配的节点。")];return;}
    [self selectOnly:[found[@"id"] unsignedLongLongValue]];
    NSRect r=nodeRect(found);
    _origin=NSMakePoint(_canvas.bounds.size.width/2-(NSMidX(r))*_zoom,_canvas.bounds.size.height/2-(NSMidY(r))*_zoom);
    _canvas.needsDisplay=YES;
    [self message:[NSString stringWithFormat:SNLocalized(@"已定位 %@ #%@"),title(typeOf(found)),found[@"id"]]];
}
-(void)undo:(id)sender {[self historyFrom:_history to:_future];}
-(void)redo:(id)sender {[self historyFrom:_future to:_history];}
-(void)historyFrom:(NSMutableArray *)from to:(NSMutableArray *)to {
    if(!from.count)return;
    NSDictionary *next=from.lastObject;
    if(_assetsWrite(@{@"graph":next})) {
        [to addObject:copyJSON(_document)];_document=copyJSON(next);[from removeLastObject];
        [self pruneSelection];[self inspect];_canvas.needsDisplay=YES;
    }
}
-(void)changed:(NSTextField *)field {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    int p=(int)field.tag;double value=field.doubleValue;
    NSScanner *scan=[NSScanner scannerWithString:field.stringValue];
    const auto& info=tri::graphParamInfo(p);
    if(![scan scanDouble:&value] || !scan.isAtEnd || !std::isfinite(value) ||
        value<info.min || value>info.max) { [self message:SNLocalized(@"参数超出范围。")];return; }
    if(tri::discreteParam(p))value=std::round(value);
    NSDictionary *previous=copyJSON(_document);
    setParam(node,p,value);
    NSSlider *slider=[_inspector viewWithTag:p+100000];
    if([slider isKindOfClass:[NSSlider class]])slider.doubleValue=value;
    [self commit:previous];[self.window makeFirstResponder:_canvas];
}
-(void)choiceChanged:(NSPopUpButton *)popup {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    const int p=(int)popup.tag;
    const auto& info=tri::graphParamInfo(p);
    // Enumerated parameters store the plain value, not the menu index.
    double value=info.min+popup.indexOfSelectedItem;
    if(value<info.min||value>info.max)return;
    NSDictionary *previous=copyJSON(_document);
    setParam(node,p,value);
    NSTextField *field=[_inspector viewWithTag:p];
    if([field isKindOfClass:[NSTextField class]])
        field.stringValue=[NSString stringWithFormat:@"%.3g",value];
    [self commit:previous];
}
-(void)nameChanged:(NSTextField *)field {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    NSString *name=[field.stringValue stringByTrimmingCharactersInSet:
                    NSCharacterSet.whitespaceAndNewlineCharacterSet];
    NSString *current=node[@"name"] ?: @"";
    if([name isEqualToString:current])return;
    NSDictionary *previous=copyJSON(_document);
    if(name.length)node[@"name"]=name;
    else [node removeObjectForKey:@"name"];
    [self commit:previous];[self inspect];
}
-(void)sliderChanged:(NSSlider *)slider {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    int p=(int)slider.tag-100000;const auto& info=tri::graphParamInfo(p);
    double value=slider.doubleValue;
    // Only declared discrete parameters are integral. The old range-width
    // heuristic snapped every 0–1 parameter (level, mix, gain, depth) to 0 or 1.
    if(tri::discreteParam(p))value=std::round(value);
    if(value<info.min||value>info.max)return;
    NSTextField *field=[_inspector viewWithTag:p];
    if([field isKindOfClass:[NSTextField class]])field.stringValue=[NSString stringWithFormat:@"%.3g",value];
    if(tri::discreteParam(p))value=std::round(value);
    NSDictionary *previous=copyJSON(_document);
    setParam(node,p,value);
    [self commit:previous];
}
-(void)trigger:(id)sender {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    if(typeOf(node)==tri::StartNode)[self resumeProcessing];
    NSDictionary *previous=copyJSON(_document);
    setParam(node,tri::TriggerPulse,([paramValue(node,tri::TriggerPulse) intValue]+1)%1000000);
    [self commit:previous];
}
-(void)bypassChanged:(NSButton *)sender {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    NSDictionary *previous=copyJSON(_document);
    if(sender.state==NSControlStateValueOn)node[@"bypass"]=@YES;
    else [node removeObjectForKey:@"bypass"];
    [self commit:previous];[self inspect];
}
-(void)triggerAll:(int)type label:(NSString*)label {
    NSMutableArray *ids=[NSMutableArray new];
    for(NSDictionary *node in _document[@"nodes"])if(typeOf(node)==type)[ids addObject:node];
    if(!ids.count){_status.stringValue=[NSString stringWithFormat:SNLocalized(@"图中没有%@"),label];return;}
    for(NSMutableDictionary *node in ids) {
        setParam(node,tri::TriggerPulse,fmodf([paramValue(node,tri::TriggerPulse) floatValue]+1,1e6f));
    }
    [self commit:nil];
    _status.stringValue=[NSString stringWithFormat:SNLocalized(@"已触发 %lu 个%@"),(unsigned long)ids.count,label];
}
-(void)resumeProcessing {
    if(_processingWrite)_processingWrite(YES);
    _processingActive=YES;[self updateProcessingControls];
}
-(void)playGraph:(id)sender {
    [self resumeProcessing];
    BOOL starts=NO;
    for(NSDictionary *node in _document[@"nodes"])if(typeOf(node)==tri::StartNode){starts=YES;break;}
    if(starts)[self triggerAll:tri::StartNode label:SNLocalized(@"开始触发器")];
    else [self message:SNLocalized(@"处理已恢复，可演奏 MIDI 或接入音频。")];
}
-(void)stopGraph:(id)sender {
    [self cancelAudition];
    if([_piano respondsToSelector:@selector(releaseNote)])[_piano performSelector:@selector(releaseNote)];
    [self cancelProcessing:nil];
    if(_processingWrite)_processingWrite(NO);
    else if(_midi)for(int ch=0;ch<16;++ch)_midi(0xb0|ch,120,0);
    _processingActive=NO;
    [self updateProcessingControls];
    [self message:SNLocalized(@"已停止播放与处理，点击播放重新启用。")];
}
-(void)panic:(id)sender {[self stopGraph:sender];}
-(void)updateProcessingControls {
    BOOL active=_processingRead?_processingRead():_processingActive;
    _processingActive=active;
    _processingLabel.stringValue=SNLocalized(active?SNLocalized(@"● 处理中"):SNLocalized(@"■ 已停止"));
    _processingLabel.textColor=active?NSColor.systemGreenColor:NSColor.secondaryLabelColor;
    _playButton.title=SNLocalized(@"播放");_stopButton.title=SNLocalized(@"停止");
    _stopButton.contentTintColor=NSColor.systemRedColor;
}
-(void)cancelProcessing:(id)sender {
    if(_exportCancellation)_exportCancellation->store(true);
    if(_exportBusy){_cancelButton.enabled=NO;[self message:SNLocalized(@"正在取消处理…")];}
}
-(BOOL)beginExport {
    if(_exportBusy){[self message:SNLocalized(@"已有导出正在进行，请等待或取消。")];return NO;}
    _exportCancellation=std::make_shared<std::atomic<bool>>(false);
    _exportBusy=YES;_cancelButton.hidden=NO;_cancelButton.enabled=YES;
    _exportProgress.hidden=NO;[_exportProgress startAnimation:nil];[self layoutUI];return YES;
}
-(void)finishExport:(NSString *)text {
    _exportBusy=NO;_cancelButton.hidden=YES;
    [_exportProgress stopAnimation:nil];_exportProgress.hidden=YES;
    _exportCancellation.reset();[self message:text];[self layoutUI];
}
-(void)toggleLibrary:(id)sender {
    _libraryVisible=!_libraryVisible;
    _libraryToggle.state=_libraryVisible?NSControlStateValueOn:NSControlStateValueOff;
    [self layoutUI];
}
-(void)languageChanged:(NSPopUpButton *)sender {SNSetEnglish(sender.indexOfSelectedItem==1);}
-(void)refreshLanguage:(NSNotification *)note {
    NSArray *fileTitles=@[SNLocalized(@"打开"),SNLocalized(@"保存"),SNLocalized(@"导出")];
    NSArray *editTitles=@[SNLocalized(@"撤销"),SNLocalized(@"重做"),SNLocalized(@"复制"),SNLocalized(@"粘贴"),SNLocalized(@"查找"),SNLocalized(@"吸附"),SNLocalized(@"排列"),SNLocalized(@"适配"),SNLocalized(@"清空")];
    for(NSUInteger i=0;i<_fileButtons.count;++i)_fileButtons[i].title=SNLocalized(fileTitles[i]);
    for(NSUInteger i=0;i<_editButtons.count;++i)_editButtons[i].title=SNLocalized(editTitles[i]);
    _libraryToggle.title=SNLocalized(@"节点库");_keyboardToggle.title=SNLocalized(@"钢琴键盘");
    _cancelButton.title=SNLocalized(@"取消处理");
    _zoomControls.lastObject.accessibilityLabel=SNLocalized(@"适配");
    if([_zoomControls.lastObject isKindOfClass:NSButton.class])((NSButton *)_zoomControls.lastObject).title=SNLocalized(@"适配");
    [_languagePopup selectItemAtIndex:SNIsEnglish()?1:0];
    [self updateProcessingControls];[self inspect];[self layoutUI];_canvas.needsDisplay=YES;
    [self message:SNLocalized(@"左侧搜索节点 · 拖动接线 · ⌘. 停止播放与处理")];
}
-(void)input:(NSButton *)sender {if(_inputWrite)_inputWrite(sender.state==NSControlStateValueOn);}
-(void)harmony:(NSTextField *)field {
    auto parsed=tri::parseProgression(field.stringValue.UTF8String);
    NSMutableDictionary *node=[self node:_selected];if(!parsed || !node){[self message:SNLocalized(@"请输入和弦名称，如 Cmaj7 | Am7 | Dm7 | G7。")];return;}
    NSDictionary *previous=copyJSON(_document);
    if(typeOf(node)==tri::ChordNode) {
        setParam(node,tri::ChordRoot,(*parsed)[0].root);
        setParam(node,tri::ChordQuality,(*parsed)[0].quality);
    } else {
        setParam(node,tri::ProgressLength,parsed->size());
        for(size_t i=0;i<parsed->size();++i){
            setParam(node,tri::ProgressRoot1+2*i,(*parsed)[i].root);
            setParam(node,tri::ProgressQuality1+2*i,(*parsed)[i].quality);
        }
    }
    [self commit:previous];[self inspect];
}
-(void)refreshClipFields {
    if(!_clipRoll)return;
    NSInteger index=_clipRoll.selectedIndex;
    if(index>=0 && index<(NSInteger)_clipRoll.notes.count) {
        NSDictionary *note=_clipRoll.notes[(NSUInteger)index];
        _clipNoteLengthField.stringValue=[NSString stringWithFormat:@"%.2f",[note[@"length"] doubleValue]];
        _clipVelocityField.stringValue=[NSString stringWithFormat:@"%d",[note[@"velocity"] intValue]];
        _clipNoteLengthField.enabled=YES;_clipVelocityField.enabled=YES;
    } else {
        _clipNoteLengthField.stringValue=@"";_clipVelocityField.stringValue=@"";
        _clipNoteLengthField.enabled=NO;_clipVelocityField.enabled=NO;
    }
    _clipLengthField.stringValue=[NSString stringWithFormat:@"%.2f",_clipRoll.lengthBeats];
}
-(void)editClip:(id)sender {
    NSMutableDictionary *node=[self node:_selected]; if(!node)return;
    tri::MidiClip clip; bool have=false;
    NSDictionary *asset=[node[@"asset"] isKindOfClass:NSDictionary.class] ? node[@"asset"] : nil;
    if([asset[@"smf"] isKindOfClass:NSString.class]) {
        NSData *data=[[NSData alloc]initWithBase64EncodedString:asset[@"smf"] options:0];
        if(data) try { clip=tri::smf::parse(static_cast<const uint8_t *>(data.bytes),data.length); have=true; } catch(...) {}
    }
    double length=have && clip.lengthBeats>0 ? clip.lengthBeats : [asset[@"beats"] doubleValue];
    if(!(length>0))length=4;
    NSMutableArray<NSMutableDictionary *> *notes=[NSMutableArray array];
    std::map<int,std::pair<double,int>> active;
    for(const auto &e:clip.events) {
        int channel=e.status&15, key=channel*128+e.data1;
        if((e.status&0xf0)==0x90 && e.data2>0) {
            active[key]={e.beat,e.data2};
        } else if((e.status&0xf0)==0x80 || (e.status&0xf0)==0x90) {
            auto it=active.find(key);if(it==active.end())continue;
            double start=it->second.first;
            double duration=std::max(.25,e.beat-start);
            [notes addObject:[@{@"pitch":@(e.data1),@"start":@(start),@"length":@(duration),
                @"velocity":@(it->second.second)} mutableCopy]];
            active.erase(it);
        }
    }
    for(const auto &entry:active) {
        int pitch=entry.first%128;
        double start=entry.second.first;
        [notes addObject:[@{@"pitch":@(pitch),@"start":@(start),
            @"length":@(std::max(.25,length-start)),@"velocity":@(entry.second.second)} mutableCopy]];
    }
    NSPanel *panel=[[NSPanel alloc]initWithContentRect:NSMakeRect(0,0,920,600)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable
        backing:NSBackingStoreBuffered defer:NO];
    panel.title=[NSString stringWithFormat:SNLocalized(@"编辑 MIDI Clip · %@"),asset[@"name"] ?: SNLocalized(@"新片段")];
    SNNoteRollView *roll=[[SNNoteRollView alloc]initWithFrame:NSMakeRect(0,112,920,488)];
    roll.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    roll.notes=notes;roll.lengthBeats=length;roll.selectedIndex=notes.count?0:-1;
    [panel.contentView addSubview:roll];
    NSTextField *clipLabel=[NSTextField labelWithString:SNLocalized(@"总长度（拍）")];
    clipLabel.frame=NSMakeRect(14,76,126,22);[panel.contentView addSubview:clipLabel];
    NSTextField *clipField=[[NSTextField alloc]initWithFrame:NSMakeRect(142,74,64,24)];
    clipField.stringValue=[NSString stringWithFormat:@"%.2f",length];
    clipField.target=self;clipField.action=@selector(clipLengthChanged:);
    [panel.contentView addSubview:clipField];
    NSTextField *noteLabel=[NSTextField labelWithString:SNLocalized(@"音符长度")];
    noteLabel.frame=NSMakeRect(224,76,92,22);[panel.contentView addSubview:noteLabel];
    NSTextField *noteField=[[NSTextField alloc]initWithFrame:NSMakeRect(318,74,64,24)];
    noteField.target=self;noteField.action=@selector(clipNoteLengthChanged:);
    [panel.contentView addSubview:noteField];
    NSTextField *velocityLabel=[NSTextField labelWithString:SNLocalized(@"力度")];
    velocityLabel.frame=NSMakeRect(400,76,58,22);[panel.contentView addSubview:velocityLabel];
    NSTextField *velocityField=[[NSTextField alloc]initWithFrame:NSMakeRect(460,74,58,24)];
    velocityField.target=self;velocityField.action=@selector(clipVelocityChanged:);
    [panel.contentView addSubview:velocityField];
    NSButton *deleteButton=[NSButton buttonWithTitle:SNLocalized(@"删除音符") target:self action:@selector(clipDelete:)];
    deleteButton.frame=NSMakeRect(424,34,110,30);[panel.contentView addSubview:deleteButton];
    NSTextField *gridLabel=[NSTextField labelWithString:SNLocalized(@"网格")];
    gridLabel.frame=NSMakeRect(542,76,44,22);[panel.contentView addSubview:gridLabel];
    NSPopUpButton *gridPopup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(590,72,84,28) pullsDown:NO];
    for(NSString *title in @[@"1/4",@"1/8",@"1/16",@"1/32"])[gridPopup addItemWithTitle:title];
    [gridPopup selectItemAtIndex:2];gridPopup.tag=3;gridPopup.target=self;gridPopup.action=@selector(clipGridChanged:);
    [panel.contentView addSubview:gridPopup];
    NSButton *audition=[NSButton buttonWithTitle:SNLocalized(@"试听全部") target:self action:@selector(clipAudition:)];
    audition.frame=NSMakeRect(14,34,110,30);[panel.contentView addSubview:audition];
    NSButton *preview=[NSButton buttonWithTitle:SNLocalized(@"试听") target:self action:@selector(clipPreview:)];
    preview.frame=NSMakeRect(130,34,90,30);[panel.contentView addSubview:preview];
    NSButton *stop=[NSButton buttonWithTitle:SNLocalized(@"停止") target:self action:@selector(stopGraph:)];
    stop.frame=NSMakeRect(226,34,90,30);stop.contentTintColor=NSColor.systemRedColor;
    stop.keyEquivalent=@".";stop.keyEquivalentModifierMask=NSEventModifierFlagCommand;[panel.contentView addSubview:stop];
    NSButton *quantize=[NSButton buttonWithTitle:SNLocalized(@"量化") target:self action:@selector(clipQuantize:)];
    quantize.frame=NSMakeRect(322,34,96,30);[panel.contentView addSubview:quantize];
    NSButton *cancel=[NSButton buttonWithTitle:SNLocalized(@"取消") target:self action:@selector(clipCancel:)];
    cancel.frame=NSMakeRect(710,34,92,30);[panel.contentView addSubview:cancel];
    NSButton *apply=[NSButton buttonWithTitle:SNLocalized(@"应用") target:self action:@selector(clipApply:)];
    apply.frame=NSMakeRect(808,34,98,30);apply.keyEquivalent=@"\r";[panel.contentView addSubview:apply];
    NSTextField *hint=[NSTextField labelWithString:SNLocalized(@"单击空白添加 · 拖动移动 · 拖右边缘改长度 · 右键/Delete 删除 · 量化按网格对齐 · 试听选中音符 / 试听全部")];
    hint.frame=NSMakeRect(14,8,892,18);hint.font=[NSFont systemFontOfSize:10];
    hint.textColor=[NSColor secondaryLabelColor];[panel.contentView addSubview:hint];
    __weak SNInstanceEditor *weakSelf=self;
    roll.selectionChanged=^{ [weakSelf refreshClipFields]; };
    self.clipPanel=panel;self.clipRoll=roll;self.clipUID=_selected;
    self.clipLengthField=clipField;self.clipNoteLengthField=noteField;self.clipVelocityField=velocityField;
    [self refreshClipFields];
    [self.window beginSheet:panel completionHandler:^(NSModalResponse response){[weakSelf cancelAudition];}];
}
-(void)cancelAudition {
    ++_auditionGeneration;
    for(NSNumber *note in _auditionNotes)if(_midi)_midi(0x80,note.intValue,0);
    [_auditionNotes removeAllObjects];
}
-(void)clipPreview:(id)sender {
    (void)sender;
    if(!_clipRoll)return;
    NSInteger index=_clipRoll.selectedIndex;
    if(index<0 || index>=(NSInteger)_clipRoll.notes.count)return;
    [self cancelAudition];[self resumeProcessing];
    uint64_t generation=_auditionGeneration;
    if(!_auditionNotes)_auditionNotes=[NSMutableSet new];
    NSDictionary *note=_clipRoll.notes[(NSUInteger)index];
    int pitch=[note[@"pitch"] intValue],velocity=[note[@"velocity"] intValue];
    [_auditionNotes addObject:@(pitch)];
    if(_midi)_midi(0x90,(uint8_t)pitch,(uint8_t)velocity);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(int64_t)(0.25*NSEC_PER_SEC)),dispatch_get_main_queue(),^{
        if(generation!=self.auditionGeneration)return;
        if(self.midi)self.midi(0x80,(uint8_t)pitch,0);
        [self.auditionNotes removeObject:@(pitch)];
    });
}
-(void)clipAudition:(id)sender {
    (void)sender;
    if(!_clipRoll||!_clipRoll.notes.count)return;
    [self cancelAudition];[self resumeProcessing];
    uint64_t generation=_auditionGeneration;
    if(!_auditionNotes)_auditionNotes=[NSMutableSet new];
    NSArray *ordered=[_clipRoll.notes sortedArrayUsingComparator:^NSComparisonResult(NSDictionary *a,NSDictionary *b) {
        double sa=[a[@"start"] doubleValue],sb=[b[@"start"] doubleValue];
        if(sa<sb)return NSOrderedAscending;
        if(sa>sb)return NSOrderedDescending;
        return [a[@"pitch"] intValue]<[b[@"pitch"] intValue]?NSOrderedAscending:NSOrderedDescending;
    }];
    double bpm=120;
    for(NSDictionary *node in _document[@"nodes"]) {
        if(typeOf(node)!=tri::ClockNode)continue;
        NSNumber *value=paramValue(node,tri::ClockBPM);
        if([value isKindOfClass:NSNumber.class] && value.doubleValue>1)bpm=value.doubleValue;
        break;
    }
    const double secondsPerBeat=60.0/bpm;
    for(NSDictionary *note in ordered) {
        int pitch=[note[@"pitch"] intValue],velocity=[note[@"velocity"] intValue];
        double start=[note[@"start"] doubleValue],length=std::max(.05,[note[@"length"] doubleValue]);
        int64_t on=(int64_t)(start*secondsPerBeat*NSEC_PER_SEC);
        int64_t off=(int64_t)((start+length)*secondsPerBeat*NSEC_PER_SEC);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,on),dispatch_get_main_queue(),^{
            if(generation!=self.auditionGeneration || !self.processingActive)return;
            [self.auditionNotes addObject:@(pitch)];
            if(self.midi)self.midi(0x90,(uint8_t)pitch,(uint8_t)velocity);
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,off),dispatch_get_main_queue(),^{
            if(generation!=self.auditionGeneration)return;
            if(self.midi)self.midi(0x80,(uint8_t)pitch,0);
            [self.auditionNotes removeObject:@(pitch)];
        });
    }
}
-(void)clipGridChanged:(id)sender {
    NSPopUpButton *popup=(NSPopUpButton *)sender;
    NSArray<NSNumber*> *divisions=@[@4,@8,@16,@32];
    if(popup.indexOfSelectedItem<0)return;
    _clipRoll.grid=4.0/divisions[(NSUInteger)popup.indexOfSelectedItem].doubleValue;
    [self refreshClipFields];
}
-(void)clipQuantize:(id)sender {
    (void)sender;
    double grid=_clipRoll.grid;
    for(NSMutableDictionary *note in _clipRoll.notes) {
        double start=[note[@"start"] doubleValue];
        double length=[note[@"length"] doubleValue];
        start=std::round(start/grid)*grid;
        start=std::max(0.0,std::min(_clipRoll.lengthBeats-grid,start));
        length=std::max(grid,std::round(length/grid)*grid);
        length=std::max(grid,std::min(_clipRoll.lengthBeats-start,length));
        note[@"start"]=@(start);note[@"length"]=@(length);
    }
    [_clipRoll setNeedsDisplay:YES];
    [self refreshClipFields];
}
-(void)clipApply:(id)sender {
    [self cancelAudition];
    NSMutableDictionary *node=[self node:_clipUID]; if(!node||!_clipRoll){[self message:SNLocalized(@"节点已不存在")];return;}
    if(!_clipRoll.notes.count){[self message:SNLocalized(@"请先添加至少一个音符")];return;}
    NSDictionary *previous=copyJSON(_document);
    tri::MidiClip clip;clip.lengthBeats=_clipRoll.lengthBeats;
    NSArray<NSDictionary *> *sorted=[_clipRoll.notes sortedArrayUsingComparator:^NSComparisonResult(NSDictionary *a,NSDictionary *b){
        return [a[@"start"] doubleValue]<[b[@"start"] doubleValue]?NSOrderedAscending:
               [a[@"start"] doubleValue]>[b[@"start"] doubleValue]?NSOrderedDescending:NSOrderedSame;
    }];
    for(NSDictionary *note in sorted) {
        double start=[note[@"start"] doubleValue],length=std::max(.25,[note[@"length"] doubleValue]);
        int pitch=std::max(0,std::min(127,[note[@"pitch"] intValue]));
        int velocity=std::max(1,std::min(127,[note[@"velocity"] intValue]));
        clip.events.push_back({start,0x90,uint8_t(pitch),uint8_t(velocity)});
        clip.events.push_back({start+length,0x80,uint8_t(pitch),0});
        clip.lengthBeats=std::max(clip.lengthBeats,start+length);
    }
    NSString *why=nil;
    NSDictionary *asset=triassets::encodeClipAsset(node[@"asset"][@"name"] ?: @"Clip",clip,&why);
    if(!asset){[self message:why ?: SNLocalized(@"无法编码 MIDI Clip")];return;}
    node[@"asset"]=asset;
    [self commit:previous];
    [self inspect];
    [self.window endSheet:_clipPanel];
    self.clipPanel=nil;self.clipRoll=nil;self.clipLengthField=nil;self.clipNoteLengthField=nil;self.clipVelocityField=nil;
}
-(void)clipCancel:(id)sender {
    [self cancelAudition];
    [self.window endSheet:_clipPanel];
    self.clipPanel=nil;self.clipRoll=nil;self.clipLengthField=nil;self.clipNoteLengthField=nil;self.clipVelocityField=nil;
}
-(void)clipDelete:(id)sender { [_clipRoll deleteSelected]; [self refreshClipFields]; }
-(void)clipLengthChanged:(id)sender {
    _clipRoll.lengthBeats=[_clipLengthField doubleValue];
    [self refreshClipFields];
}
-(void)clipNoteLengthChanged:(id)sender {
    NSInteger index=_clipRoll.selectedIndex;
    if(index<0||index>=(NSInteger)_clipRoll.notes.count)return;
    _clipRoll.notes[(NSUInteger)index][@"length"]=@(std::max(.25,[_clipNoteLengthField doubleValue]));
    [_clipRoll setNeedsDisplay:YES];[self refreshClipFields];
}
-(void)clipVelocityChanged:(id)sender {
    NSInteger index=_clipRoll.selectedIndex;
    if(index<0||index>=(NSInteger)_clipRoll.notes.count)return;
    _clipRoll.notes[(NSUInteger)index][@"velocity"]=@(std::max(1,std::min(127,[_clipVelocityField intValue])));
    [_clipRoll setNeedsDisplay:YES];[self refreshClipFields];
}
-(void)import:(id)sender {
    if(_exportBusy){[self message:SNLocalized(@"已有处理正在进行，请等待或取消。")];return;}
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    uint64_t uid=_selected;
    int nodeType=typeOf(node);
    BOOL clip=nodeType==tri::ClipNode,audio=nodeType==tri::SamplerNode || nodeType==tri::AudioClipNode;
    NSOpenPanel *panel=[NSOpenPanel openPanel];panel.canChooseDirectories=!clip && !audio;
    NSArray *extensions=clip?@[@"mid",@"midi"]:audio?@[@"wav",@"aif",@"aiff",@"aifc",@"mp3",@"m4a",@"aac",@"caf",@"flac"]:
        @[@"mlmodel",@"mlmodelc",@"mlpackage",@"json"];
    NSMutableArray *types=[NSMutableArray new];
    for(NSString *ext in extensions) {
        UTType *type=[UTType typeWithFilenameExtension:ext];if(type)[types addObject:type];
    }
    panel.allowedContentTypes=types;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result!=NSModalResponseOK)return;
        if(![self beginExport])return;
        auto cancelled=self.exportCancellation;
        [self message:SNLocalized(@"正在载入素材…")];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
            NSString *kind=clip?@"clip":audio?@"audio":@"model";
            NSString *why=nil;NSDictionary *asset=triassets::importAsset(kind,panel.URL,&why);
            dispatch_async(dispatch_get_main_queue(),^{
                if(cancelled->load()){[self finishExport:SNLocalized(@"已取消素材载入。")];return;}
                NSMutableDictionary *current=[self node:uid];
                if(!asset){[self finishExport:why];return;}
                if(current!=node){[self finishExport:SNLocalized(@"节点已改变，未应用素材。")];return;}
                [self finishExport:SNLocalized(@"素材已载入。")];
                NSDictionary *previous=copyJSON(self.document);current[@"asset"]=copyJSON(asset);
                [self commit:previous];[self inspect];
            });
        });
    }];
}
-(void)label:(NSString *)text y:(double)y {
    NSTextField *label=[NSTextField wrappingLabelWithString:text];
    label.frame=NSMakeRect(14,y,270,48);label.font=[NSFont systemFontOfSize:12];label.textColor=NSColor.secondaryLabelColor;
    [_inspector addSubview:label];
}
-(void)rangeChanged:(NSTextField *)field {
    NSMutableDictionary *node=[self node:_selected];
    if(typeOf(node)!=tri::ArrangerNode)return;
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
            [self message:SNLocalized(@"音域需为 0–127 的整数，最低音不能高于最高音。")];
            return;
        }
        NSDictionary *previous=copyJSON(_document);
        setParam(node,pid,value);
        [self commit:previous];[self inspect];return;
    }
}
-(void)textureChanged:(NSPopUpButton *)popup {
    NSMutableDictionary *node=[self node:_selected];
    if(typeOf(node)!=tri::ArrangerNode)return;
    NSDictionary *previous=copyJSON(_document);
    setParam(node,(int)popup.tag,popup.indexOfSelectedItem);
    [self commit:previous];[self inspect];
}
-(double)arrangerInspector:(NSDictionary *)node y:(double)y {
    [self label:SNLocalized(@"音域用 MIDI 音号表示（60 = C4）。选出的音按八度移入各声部音域。") y:y];
    y+=46;
    for(int i=0;i<3;++i) {
        auto voice=voiceControls[i];
        NSString *name=voiceNames()[i];
        int lower=voiceValue(node,voice.lower).intValue,upper=voiceValue(node,voice.upper).intValue;
        NSTextField *heading=[NSTextField labelWithString:[NSString stringWithFormat:@"%@   %@ – %@",name,pitchName(lower),pitchName(upper)]];
        heading.font=[NSFont boldSystemFontOfSize:13];heading.frame=NSMakeRect(14,y,270,22);
        heading.textColor=[NSColor colorWithCalibratedRed:.65 green:.79 blue:.96 alpha:1];
        [_inspector addSubview:heading];y+=29;
        for(int bound=0;bound<2;++bound) {
            NSString *caption=bound?SNLocalized(@"最高音"):SNLocalized(@"最低音");
            double x=bound?155:14;
            NSTextField *label=[NSTextField labelWithString:caption];
            label.font=[NSFont systemFontOfSize:12];
            label.frame=NSMakeRect(x,y,129,21);[_inspector addSubview:label];
            NSTextField *field=[[NSTextField alloc]initWithFrame:NSMakeRect(x,y+24,129,25)];
            field.integerValue=bound?upper:lower;field.tag=bound?voice.upper:voice.lower;
            field.target=self;field.action=@selector(rangeChanged:);
            [field setAccessibilityLabel:[name stringByAppendingString:caption]];
            [_inspector addSubview:field];
        }
        y+=57;
        NSTextField *label=[NSTextField labelWithString:SNLocalized(@"织体")];
        label.font=[NSFont systemFontOfSize:12];
        label.frame=NSMakeRect(14,y,270,20);[_inspector addSubview:label];
        NSPopUpButton *popup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(14,y+22,270,27) pullsDown:NO];
        [popup addItemsWithTitles:@[SNLocalized(@"最高音"),SNLocalized(@"中间音"),SNLocalized(@"最低音"),SNLocalized(@"柱式和弦（全部音）")]];
        [popup selectItemAtIndex:voiceValue(node,voice.texture).integerValue];
        popup.tag=voice.texture;popup.target=self;popup.action=@selector(textureChanged:);
        [popup setAccessibilityLabel:[name stringByAppendingString:SNLocalized(@"织体")]];
        [_inspector addSubview:popup];y+=65;
    }
    return y;
}
-(void)toggleParameterGroup:(NSButton *)sender {
    if([_collapsedParameterGroups containsObject:sender.identifier])[_collapsedParameterGroups removeObject:sender.identifier];
    else [_collapsedParameterGroups addObject:sender.identifier];
    [self inspect];
}
-(void)refreshAiStatus {
    if(!_aiStatusField)return;
    if(_aiBusy) {
        const int p=aiProgressShared?aiProgressShared->load():-1;
        _aiStatusField.stringValue=p>=0?[NSString stringWithFormat:SNLocalized(@"正在生成 %d%%"),p]:
                                          SNLocalized(@"正在生成…");
        if(_aiProgressView)_aiProgressView.hidden=(p>=0);
    } else {
        _aiStatusField.stringValue=_aiStatusText.length?_aiStatusText:SNLocalized(@"空闲");
        if(_aiProgressView)_aiProgressView.hidden=YES;
    }
}
// Submitting is an explicit action: a two-minute model run must never start
// because the transport rolled. The trigger port still starts playback.
-(void)generateAudio:(id)sender {
    if(_aiBusy) {[self message:SNLocalized(@"已有生成任务在进行。")];return;}
    NSMutableDictionary *node=[self node:_selected];
    if(!node||typeOf(node)!=tri::AiMusicNode)return;
    const BOOL feedback=aiUseScore;aiUseScore=NO;
    NSDictionary *texts=[node[@"texts"] isKindOfClass:NSDictionary.class]?node[@"texts"]:@{};
    NSString *prompt=texts[@"ai.prompt"] ?: @"";
    if(!prompt.length) {[self message:SNLocalized(@"请先填写风格描述。")];return;}
    tri::GenRequest request;
    request.backend=int(paramValue(node,tri::AiMusicBackend).doubleValue);
    request.model=int(paramValue(node,tri::AiMusicModel).doubleValue);
    request.quality=int(paramValue(node,tri::AiMusicQuality).doubleValue);
    request.outMode=int(paramValue(node,tri::AiMusicOutMode).doubleValue);
    request.seconds=paramValue(node,tri::AiMusicDuration).doubleValue;
    request.seed=unsigned(paramValue(node,tri::AiMusicSeed).doubleValue);
    NSString *lyrics=texts[@"ai.lyrics"] ?: @"";
    NSString *execPath=texts[@"ai.execPath"] ?: @"";
    NSString *modelPath=texts[@"ai.modelPath"] ?: @"";
    NSString *extraArgs=texts[@"ai.extraArgs"] ?: @"";
    NSString *score=texts[@"ai.score"] ?: @"";
    if(feedback&&!score.length) {[self message:SNLocalized(@"乐谱为空，请先粘贴或生成乐谱。")];return;}
    request.prompt=prompt.UTF8String ?: "";
    request.lyrics=lyrics.UTF8String ?: "";
    request.abcText=feedback?(score.UTF8String ?: ""):"";
    request.execPath=execPath.UTF8String ?: "";
    request.modelPath=modelPath.UTF8String ?: "";
    request.extraArgs=extraArgs.UTF8String ?: "";
    NSString *folder=[NSTemporaryDirectory() stringByAppendingPathComponent:
                      [@"soundnet-ai-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    if(![[NSFileManager defaultManager] createDirectoryAtPath:folder withIntermediateDirectories:YES
                                  attributes:nil error:nil]) {
        [self message:SNLocalized(@"无法创建生成工作目录。")];return;
    }
    request.workDir=folder.UTF8String;
    const uint64_t uid=_selected;
    NSDictionary *documentBefore=copyJSON(_document);
    auto cancelFlag=std::make_shared<std::atomic<bool>>(false);
    auto progress=std::make_shared<std::atomic<int>>(-1);
    aiCancellation=cancelFlag;aiProgressShared=progress;
    _aiBusy=YES;_aiNode=uid;_aiStatusText=SNLocalized(@"正在生成…");[self refreshAiStatus];
    auto backend=std::make_shared<tri::AudioCppCliBackend>();
    __weak SNInstanceEditor *weakSelf=self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        @autoreleasepool {
            std::string why;
            uint64_t task=0;
            if(!backend->available(why)||!backend->submit(request,task,why)) {
                NSString *reason=[NSString stringWithUTF8String:why.c_str()];
                dispatch_async(dispatch_get_main_queue(),^{
                    SNInstanceEditor *strong=weakSelf;if(!strong)return;
                    strong->_aiBusy=NO;strong->_aiStatusText=reason ?: SNLocalized(@"无法启动生成。");
                    [strong refreshAiStatus];[strong message:strong->_aiStatusText];
                });
                return;
            }
            bool cancelled=false;
            while(true) {
                if(cancelFlag->load()) {
                    if(!cancelled){cancelled=true;backend->cancel(task);}
                }
                tri::GenResult result;
                const auto status=backend->poll(task,result,why);
                if(status==tri::GenTaskStatus::Running) {
                    progress->store(backend->progress(task));
                    [NSThread sleepForTimeInterval:.3];
                    continue;
                }
                if(status==tri::GenTaskStatus::Succeeded) {
                    NSString *importWhy=nil;
                    const bool wantAudio=!result.wavPath.empty();
                    NSDictionary *asset=wantAudio?triassets::importAsset(@"audio",
                        [NSURL fileURLWithPath:[NSString stringWithUTF8String:result.wavPath.c_str()]],&importWhy):nil;
                    const bool hadScore=!result.abcPath.empty();
                    NSString *score=nil;
                    if(hadScore) {
                        NSString *path=[NSString stringWithUTF8String:result.abcPath.c_str()];
                        score=[NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
                    }
                    dispatch_async(dispatch_get_main_queue(),^{
                        SNInstanceEditor *strong=weakSelf;if(!strong)return;
                        strong->_aiBusy=NO;
                        NSMutableDictionary *current=[strong node:uid];
                        if(!asset&&wantAudio) {strong->_aiStatusText=importWhy ?: SNLocalized(@"生成结果无法导入。");[strong refreshAiStatus];[strong message:strong->_aiStatusText];return;}
                        if(!current||strong.document!=documentBefore) {
                            strong->_aiStatusText=SNLocalized(@"节点已改变，未应用生成结果。");[strong refreshAiStatus];return;
                        }
                        NSDictionary *previous=copyJSON(strong.document);
                        if(asset) current[@"asset"]=copyJSON(asset);
                        if(score.length) {
                            NSMutableDictionary *texts=[current[@"texts"] isKindOfClass:NSDictionary.class]
                                ? [current[@"texts"] mutableCopy] : [NSMutableDictionary new];
                            texts[@"ai.score"]=score;
                            current[@"texts"]=texts;
                        }
                        strong->_aiStatusText=!wantAudio?SNLocalized(@"已生成乐谱（仅乐谱模式）。"):
                            (hadScore?SNLocalized(@"生成完成（含乐谱）。"):SNLocalized(@"生成完成。"));
                        [strong commit:previous];[strong inspect];
                        [strong message:strong->_aiStatusText];
                    });
                    return;
                }
                NSString *reason=[NSString stringWithUTF8String:why.c_str()];
                dispatch_async(dispatch_get_main_queue(),^{
                    SNInstanceEditor *strong=weakSelf;if(!strong)return;
                    strong->_aiBusy=NO;
                    strong->_aiStatusText=reason ?: SNLocalized(@"生成失败。");
                    [strong refreshAiStatus];[strong message:strong->_aiStatusText];
                });
                return;
            }
        }
    });
}
// Re-render from the score the user edited instead of planning a new one
// (audio.cpp `abc_file`). This is the return half of the score loop.
-(void)generateFromScore:(id)sender {
    aiUseScore=YES;
    [self generateAudio:sender];
}
// The score loop's other half: ABC becomes an ordinary MIDI clip, so the piano
// roll, the scale quantiser, the arranger and MIDI export all apply to it.
-(void)importScore:(id)sender {
    NSMutableDictionary *node=[self node:_selected];
    if(!node||typeOf(node)!=tri::AiMusicNode)return;
    id raw=node[@"texts"][@"ai.score"];
    NSString *text=[raw isKindOfClass:NSString.class]?raw:@"";
    if(!text.length) {[self message:SNLocalized(@"还没有乐谱，请先生成或粘贴 ABC 文本。")];return;}
    tri::abc::Score score;std::string why;
    if(!tri::abc::parse(text.UTF8String ?: "",score,why)) {
        [self message:[NSString stringWithFormat:SNLocalized(@"乐谱解析失败：%@"),
            [NSString stringWithUTF8String:why.c_str()]]];
        return;
    }
    tri::MidiClip clip=tri::abc::toClip(score);
    NSString *assetWhy=nil;
    NSDictionary *asset=triassets::encodeClipAsset(@"score.abc",clip,&assetWhy);
    if(!asset) {[self message:assetWhy ?: SNLocalized(@"乐谱无法转换为 MIDI 剪辑。")];return;}
    NSDictionary *previous=copyJSON(_document);
    uint64_t next=1;
    for(NSDictionary *each in _document[@"nodes"])next=std::max(next,[each[@"id"] unsignedLongLongValue]+1);
    NSMutableDictionary *created=[self makeNode:tri::ClipNode
        at:NSMakePoint([node[@"x"] doubleValue]+30,[node[@"y"] doubleValue]+150) id:next];
    created[@"asset"]=copyJSON(asset);
    // The original ABC rides along so the project stays self-describing even if
    // the generating node is deleted later.
    created[@"texts"]=@{@"clip.score":text};
    // Mirror the note-accepting downstream edges: the score can replace the
    // audio without rewiring, while audio-only targets are left to the user.
    for(NSDictionary *edge in [_document[@"edges"] copy]) {
        if([edge[@"source"] unsignedLongLongValue]!=[node[@"id"] unsignedLongLongValue])continue;
        NSDictionary *target=[self node:[edge[@"target"] unsignedLongLongValue]];
        if(!target)continue;
        const int targetType=typeOf(target);
        const int port=std::max(0,inputPortOf(target,edge));
        const tri::PortSpec *portSpec=tri::nodePort(targetType,true,size_t(port));
        if(!portSpec||portSpec->signal!=tri::Signal::Midi)continue;
        [_document[@"edges"] addObject:makeEdge(next,[edge[@"target"] unsignedLongLongValue],
                                                targetType,port,tri::ClipNode,0)];
    }
    _selected=next;_selectedEdge=-1;
    [_selection removeAllObjects];[_selection addObject:@(next)];
    [self commit:previous];[self inspect];
    NSString *summary=[NSString stringWithFormat:SNLocalized(@"已创建 MIDI 剪辑：%lu 个音符。"),
                       (unsigned long)score.notes.size()];
    if(!score.warnings.empty()) {
        summary=[summary stringByAppendingFormat:@" %@",
                 [NSString stringWithUTF8String:score.warnings.front().c_str()]];
    }
    [self message:summary];
}
-(void)aiStop:(id)sender {
    if(_aiBusy) {
        if(aiCancellation)aiCancellation->store(true);
        [self message:SNLocalized(@"正在取消生成…")];
        return;
    }
    if(_nodeStop) {_nodeStop(_selected);[self message:SNLocalized(@"已停止该节点。")];}
    else [self message:SNLocalized(@"当前环境不支持节点级停止。")];
}
-(void)textChanged:(NSTextField *)field {
    NSMutableDictionary *node=[self node:_selected];
    if(!node||typeOf(node)!=tri::AiMusicNode)return;
    const tri::TextSpec *spec=tri::nodeText(tri::AiMusicNode,size_t(field.tag));
    if(!spec)return;
    NSMutableDictionary *texts=[node[@"texts"] isKindOfClass:NSDictionary.class]
        ? [node[@"texts"] mutableCopy] : [NSMutableDictionary new];
    NSString *key=[NSString stringWithUTF8String:spec->id];
    NSString *value=field.stringValue ?: @"";
    if([(texts[key] ?: @"") isEqualToString:value])return;
    const NSUInteger limit=256*1024;
    if(value.length>limit) {[self message:SNLocalized(@"文本过长。")];return;}
    NSDictionary *previous=copyJSON(_document);
    texts[key]=value;
    node[@"texts"]=texts;
    [self commit:previous];
}
-(void)inspect {
    NSPoint scrollOrigin=_inspectorScroll.contentView.bounds.origin;
    BOOL sameNode=_inspectedUID==_selected;_inspectedUID=_selected;
    for(NSView *view in _inspector.subviews.copy)[view removeFromSuperview];
    if(_selection.count>1) {
        [self label:[NSString stringWithFormat:SNLocalized(@"已选择 %lu 个节点"),(unsigned long)_selection.count] y:12];
        [self label:SNLocalized(@"Shift 点击增减选择，拖动空白框选。拖动任一节点可整体移动。") y:40];
        [self button:SNLocalized(@"复制节点") frame:NSMakeRect(12,78,105,28) action:@selector(duplicate:) parent:_inspector];
        [self button:SNLocalized(@"删除节点") frame:NSMakeRect(132,78,105,28) action:@selector(remove:) parent:_inspector];
        [self button:SNLocalized(@"断开连线") frame:NSMakeRect(12,112,105,28) action:@selector(disconnect:) parent:_inspector];
        [self button:SNLocalized(@"取消选择") frame:NSMakeRect(132,112,105,28) action:@selector(selectNone:) parent:_inspector];
        _inspector.frame=NSMakeRect(0,0,298,_inspectorScroll.contentSize.height);return;
    }
    NSMutableDictionary *node=[self node:_selected];
    if(!node){[self label:SNLocalized(@"选择节点以编辑参数。可在左侧搜索并拖入节点，拖动端口连接信号。") y:18];
        _inspector.frame=NSMakeRect(0,0,298,_inspectorScroll.contentSize.height);return;}
    int type=typeOf(node);
    NSTextField *nodeHeading=[NSTextField labelWithString:[NSString stringWithFormat:@"%@  #%llu",nodeLabel(node),_selected]];
    nodeHeading.frame=NSMakeRect(14,14,270,24);nodeHeading.font=[NSFont systemFontOfSize:15 weight:NSFontWeightSemibold];
    [_inspector addSubview:nodeHeading];
    NSTextField *nameField=[[NSTextField alloc]initWithFrame:NSMakeRect(14,43,270,26)];
    nameField.stringValue=node[@"name"] ?: @"";nameField.placeholderString=SNLocalized(@"自定义名称（可选）");
    nameField.tag=990001;nameField.target=self;nameField.action=@selector(nameChanged:);
    [nameField setAccessibilityLabel:SNLocalized(@"节点名称")];
    [_inspector addSubview:nameField];
    [self button:SNLocalized(@"复制节点") frame:NSMakeRect(14,78,129,28) action:@selector(duplicate:) parent:_inspector];
    [self button:SNLocalized(@"删除节点") frame:NSMakeRect(155,78,129,28) action:@selector(remove:) parent:_inspector];
    double y=120;
    const tri::NodeMeta *meta=tri::nodeMeta(type);
    if(meta && meta->bypassable) {
        NSButton *bypass=[NSButton checkboxWithTitle:SNLocalized(@"旁通此节点（保留连线）") target:self action:@selector(bypassChanged:)];
        bypass.frame=NSMakeRect(14,y,270,28);bypass.state=node[@"bypass"] ? NSControlStateValueOn : NSControlStateValueOff;
        [_inspector addSubview:bypass];y+=38;
    }
    auto presets=presetsFor(type);
    if(!presets.empty()) {
        [self label:SNLocalized(@"预设") y:y];
        NSPopUpButton *popup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(76,y-4,208,26) pullsDown:NO];
        [popup addItemWithTitle:SNLocalized(@"选择预设…")];
        for(const auto& preset:presets)[popup addItemWithTitle:SNLocalized([NSString stringWithUTF8String:preset.name])];
        popup.target=self;popup.action=@selector(applyPreset:);
        [_inspector addSubview:popup];y+=38;
    }
    if(!controls(type).empty() || type==tri::ArrangerNode) {
        [self button:SNLocalized(@"恢复默认参数") frame:NSMakeRect(14,y,270,28) action:@selector(resetParams:) parent:_inspector];y+=38;
    }
    if(type==tri::AiMusicNode) {
        [self label:SNLocalized(@"生成在本机后台进行，产物作为工程素材嵌入；「停止」可取消生成或停止本节点播放。") y:y];y+=48;
        for(int i=0;i<tri::nodeTextCount(type);++i) {
            const tri::TextSpec *spec=tri::nodeText(type,size_t(i));
            NSString *caption=SNLocalized([NSString stringWithUTF8String:spec->label]);
            NSTextField *label=[NSTextField labelWithString:caption];
            label.font=[NSFont systemFontOfSize:12];label.frame=NSMakeRect(14,y,270,18);
            [_inspector addSubview:label];y+=20;
            NSString *key=[NSString stringWithUTF8String:spec->id];
            NSString *value=node[@"texts"][key] ?: @"";
            const bool multiline=std::strcmp(spec->id,"ai.lyrics")==0;
            NSTextField *field=[[NSTextField alloc]initWithFrame:NSMakeRect(14,y,270,multiline?66:25)];
            field.stringValue=value;field.tag=i;field.target=self;field.action=@selector(textChanged:);
            field.font=[NSFont systemFontOfSize:12];
            field.placeholderString=multiline?SNLocalized(@"每行一句，可用 [Verse] [Chorus] 标记段落"):
                                            SNLocalized(@"留空自动探测");
            if(multiline) {field.cell.wraps=YES;field.usesSingleLineMode=NO;}
            field.accessibilityLabel=caption;[_inspector addSubview:field];
            y+=multiline?74:33;
        }
        NSTextField *state=[NSTextField labelWithString:@""];
        state.font=[NSFont systemFontOfSize:11 weight:NSFontWeightMedium];
        state.textColor=[NSColor colorWithCalibratedRed:.65 green:.79 blue:.96 alpha:1];
        state.frame=NSMakeRect(14,y,270,18);_aiStatusField=state;[_inspector addSubview:state];
        NSProgressIndicator *spinner=[[NSProgressIndicator alloc]initWithFrame:NSMakeRect(240,y-2,20,16)];
        spinner.style=NSProgressIndicatorStyleSpinning;spinner.controlSize=NSControlSizeSmall;
        _aiProgressView=spinner;[_inspector addSubview:spinner];
        [self refreshAiStatus];y+=26;
        NSButton *generate=[self button:SNLocalized(@"生成音频") frame:NSMakeRect(14,y,129,30) action:@selector(generateAudio:) parent:_inspector];
        generate.enabled=!_aiBusy;
        NSButton *stop=[self button:SNLocalized(@"停止") frame:NSMakeRect(155,y,129,30) action:@selector(aiStop:) parent:_inspector];
        stop.enabled=_aiBusy||_nodeStop!=nil;
        y+=40;
        // The score loop: ABC -> MIDI clip for editing, and edit -> re-render.
        id rawScore=node[@"texts"][@"ai.score"];
        const BOOL hasScore=[rawScore isKindOfClass:NSString.class]&&[rawScore length]>0;
        NSButton *importClip=[self button:SNLocalized(@"导入为 MIDI 剪辑") frame:NSMakeRect(14,y,270,28)
                              action:@selector(importScore:) parent:_inspector];
        importClip.enabled=hasScore&&!_aiBusy;
        importClip.toolTip=SNLocalized(@"把乐谱转换成可编辑的 MIDI 剪辑，钢琴卷帘随即可用。");
        y+=36;
        NSButton *fromScore=[self button:SNLocalized(@"按乐谱重新生成") frame:NSMakeRect(14,y,270,28)
                             action:@selector(generateFromScore:) parent:_inspector];
        fromScore.enabled=hasScore&&!_aiBusy;
        fromScore.toolTip=SNLocalized(@"把改动后的乐谱回喂后端重新渲染，风格描述与歌词不参与。");
        y+=40;
    }
    if(type==tri::ClipNode) {
        id rawScore=node[@"texts"][@"clip.score"];
        if([rawScore isKindOfClass:NSString.class]&&[rawScore length]) {
            [self label:SNLocalized(@"来源乐谱（ABC，只读）") y:y];y+=24;
            NSTextField *view=[[NSTextField alloc]initWithFrame:NSMakeRect(14,y,270,72)];
            view.stringValue=rawScore;view.editable=NO;view.selectable=YES;
            view.font=[NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
            view.cell.wraps=YES;view.usesSingleLineMode=NO;
            view.toolTip=SNLocalized(@"这段 ABC 由 AI 生成节点导入，用于追溯与再生成。");
            [_inspector addSubview:view];y+=84;
        }
    }
    if(type==tri::ArrangerNode)y=[self arrangerInspector:node y:y];
    else {
        std::vector<tri::ParamGroup> groups;
        for(int p:controls(type)) {
            if(p==tri::TriggerPulse)continue; // A trigger is an action, not an editable counter.
            auto group=tri::paramSpec(p)->group;
            if(std::find(groups.begin(),groups.end(),group)==groups.end())groups.push_back(group);
        }
        for(auto group:groups) {
            NSString *key=[NSString stringWithFormat:@"%d:%d",type,int(group)];
            BOOL collapsed=[_collapsedParameterGroups containsObject:key];
            NSButton *section=[NSButton buttonWithTitle:[NSString stringWithFormat:@"%@  %@",collapsed?@"▸":@"▾",SNLocalized(@(tri::paramGroupLabel(group)))]
                target:self action:@selector(toggleParameterGroup:)];
            section.identifier=key;section.tag=990200+int(group);section.frame=NSMakeRect(10,y,278,28);
            section.bordered=NO;section.alignment=NSTextAlignmentLeft;
            section.font=[NSFont systemFontOfSize:12 weight:NSFontWeightSemibold];
            section.attributedTitle=[[NSAttributedString alloc]initWithString:section.title attributes:@{
                NSFontAttributeName:section.font,
                NSForegroundColorAttributeName:[NSColor colorWithCalibratedRed:.65 green:.79 blue:.96 alpha:1]}];
            section.wantsLayer=YES;section.layer.cornerRadius=5;
            section.layer.backgroundColor=[NSColor colorWithCalibratedRed:.18 green:.23 blue:.30 alpha:1].CGColor;
            [_inspector addSubview:section];y+=34;
            if(collapsed){y+=6;continue;}
            for(int p:controls(type)) {
                if(p==tri::TriggerPulse || tri::paramSpec(p)->group!=group)continue;
                const auto& info=tri::graphParamInfo(p);id val=paramValue(node,p);
                NSString *caption=parameterName(p);
                if(info.unit && *info.unit)caption=[caption stringByAppendingFormat:@" · %s",info.unit];
                NSTextField *label=[NSTextField labelWithString:caption];
                label.frame=NSMakeRect(16,y+3,184,20);label.font=[NSFont systemFontOfSize:12];
                label.toolTip=caption;[_inspector addSubview:label];
                NSArray<NSString*> *options=paramOptions(p);
                if(options) {
                    label.frame=NSMakeRect(16,y,268,20);
                    NSPopUpButton *popup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(14,y+22,270,27) pullsDown:NO];
                    [popup addItemsWithTitles:options];
                    NSInteger index=(NSInteger)std::lround([val doubleValue]-info.min);
                    [popup selectItemAtIndex:std::clamp<NSInteger>(index,0,(NSInteger)options.count-1)];
                    popup.tag=p;popup.target=self;popup.action=@selector(choiceChanged:);
                    [popup setAccessibilityLabel:parameterName(p)];[_inspector addSubview:popup];y+=61;continue;
                }
                NSTextField *field=[[NSTextField alloc]initWithFrame:NSMakeRect(206,y,78,25)];
                field.stringValue=[NSString stringWithFormat:@"%.4g",[val doubleValue]];
                field.alignment=NSTextAlignmentRight;field.font=[NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular];
                field.tag=p;field.target=self;field.action=@selector(changed:);
                field.accessibilityLabel=parameterName(p);[_inspector addSubview:field];
                if(info.max-info.min<=20000) {
                    NSSlider *slider=[NSSlider sliderWithValue:[val doubleValue] minValue:info.min maxValue:info.max target:self action:@selector(sliderChanged:)];
                    slider.frame=NSMakeRect(14,y+28,270,18);slider.tag=p+100000;slider.continuous=NO;
                    slider.accessibilityLabel=parameterName(p);[_inspector addSubview:slider];y+=60;
                } else y+=39;
            }
            y+=10;
        }
    }
    if(type==tri::StartNode || type==tri::StopNode){
        [self button:SNLocalized(@"发送触发信号") frame:NSMakeRect(14,y,270,30) action:@selector(trigger:) parent:_inspector];y+=44;
    }
    if(type==tri::WaitNode){[self label:SNLocalized(@"收到开始或停止信号后，等待指定秒数再转发。可排队等待多个信号。") y:y];y+=65;}
    if(type==tri::CountNode){[self label:SNLocalized(@"每收到 X 个信号后输出一次，再开始下一轮计数。可连接 Clip 的循环完成端口。") y:y];y+=65;}
    if(type==tri::RandomNode){[self label:SNLocalized(@"收到触发后按 Chance 概率决定是否继续转发，适合生成随机段落或概率门限。") y:y];y+=52;}
    if(type==tri::MetronomeNode){[self label:SNLocalized(@"开始触发后按 BPM 产生节拍，停止触发后静音。Beats 控制每小节拍数，Accent 控制第一拍重音。") y:y];y+=65;}
    if(type==tri::TransposeNode){[self label:SNLocalized(@"把输入音符按半音移调，范围外自动夹到 0–127。旁通时保持原音高。") y:y];y+=52;}
    if(type==tri::EchoNode){[self label:SNLocalized(@"为输入音符安排多次延迟重复，每次可按 Pitch 移调并按 Feedback 衰减力度。原音立即通过。") y:y];y+=65;}
    if(type==tri::GateNode){[self label:SNLocalized(@"低于 Threshold 的音频会被压低；Range 控制关闭时的衰减深度，Attack/Release 控制开合速度。") y:y];y+=65;}
    if(type==tri::ChannelFilterNode){[self label:SNLocalized(@"只放行指定 MIDI 通道（1–16）；0 表示全部通道，旁通时不过滤。") y:y];y+=60;}
    if(type==tri::CommentNode){[self label:SNLocalized(@"注释节点不参与音频或 MIDI；用上方名称字段记录说明文字。") y:y];y+=50;}
    if(type==tri::ClipNode || type==tri::ModelNode) {
        [self button:type==tri::ClipNode?SNLocalized(@"载入 MIDI Clip…"):SNLocalized(@"载入音符模型…") frame:NSMakeRect(14,y,270,30) action:@selector(import:) parent:_inspector];y+=40;
        if(type==tri::ClipNode) {
            [self button:SNLocalized(@"编辑音符…") frame:NSMakeRect(14,y,270,30) action:@selector(editClip:) parent:_inspector];y+=40;
        }
        [self label:node[@"asset"][@"name"] ?: SNLocalized(@"未载入素材") y:y];y+=46;
    }
    if(type==tri::SamplerNode || type==tri::AudioClipNode || type==tri::AiMusicNode) {
        [self button:SNLocalized(@"载入音频（WAV / AIFF / MP3 / M4A / AAC / CAF / FLAC）…") frame:NSMakeRect(14,y,270,30) action:@selector(import:) parent:_inspector];y+=42;
        NSDictionary *asset=node[@"asset"];
        if(asset[@"name"]) {
            [self label:asset[@"name"] y:y];y+=24;
            double rate=[asset[@"rate"] doubleValue];int channels=[asset[@"channels"] intValue];
            NSData *pcm=[[NSData alloc]initWithBase64EncodedString:asset[@"pcm"] ?: @"" options:0];
            double seconds=rate>0 ? double(pcm.length)/4.0/double(std::max(1,channels))/rate : 0;
            [self label:[NSString stringWithFormat:SNLocalized(@"%.2f 秒 · %@ · %.0f Hz"),seconds,channels==1?SNLocalized(@"单声道"):SNLocalized(@"立体声"),rate] y:y];y+=26;
            SNWaveformView *wave=[[SNWaveformView alloc]initWithFrame:NSMakeRect(14,y,270,58)];
            wave.pcm=pcm;wave.channels=channels;[_inspector addSubview:wave];y+=66;
        } else [self label:SNLocalized(@"未载入音频素材") y:y],y+=28;
        if(type==tri::SamplerNode)
            [self label:SNLocalized(@"音符触发复音采样。Root 为原始音高，Start/End 为采样区间，Loop 循环，Attack/Release 消除爆音；Reverse 反向播放，Velocity 为力度敏感度。") y:y],y+=74;
        else if(type==tri::AiMusicNode)
            [self label:SNLocalized(@"生成产物按音频片段播放：触发端口控制开始/停止，Loop 循环，Fade 淡化，Pitch 移调。生成只在检查器内手动发起。") y:y],y+=76;
        else
            [self label:SNLocalized(@"音频片段由开始/停止触发器控制。Loop 循环，Start/End 为片段区间，Fade 淡入淡出，Gain 为增益；Pitch 为半音移调，Reverse 反向播放。") y:y],y+=76;
    }
    if(type==tri::ChordNode || type==tri::ProgressNode) {
        [self label:SNLocalized(@"和弦 / 走向，可手动输入") y:y];y+=28;
        NSTextField *field=[[NSTextField alloc]initWithFrame:NSMakeRect(14,y,270,28)];
        field.placeholderString=type==tri::ChordNode?@"Cmaj7":@"Cmaj7 | Am7 | Dm7 | G7";
        field.target=self;field.action=@selector(harmony:);[_inspector addSubview:field];y+=40;
        [self button:SNLocalized(@"预制 Cmaj7 / 爵士走向") frame:NSMakeRect(14,y,270,28) action:@selector(preset:) parent:_inspector];y+=42;
    }
    if(type==tri::InputNode && _inputRead) {
        NSButton *button=[NSButton checkboxWithTitle:SNLocalized(@"启用硬件音频输入") target:self action:@selector(input:)];
        button.frame=NSMakeRect(14,y,270,28);button.state=_inputRead();[_inspector addSubview:button];y+=42;
    }
    if(type==tri::Osc1Node){[self label:SNLocalized(@"Wave  0 正弦  1 方波  2 锯齿  3 三角") y:y];y+=50;}
    if(type==tri::FilterNode){[self label:SNLocalized(@"Type  0 低通  1 高通  2 带通\nCutoff 20–20000 Hz，Resonance 为 Q 值。") y:y];y+=62;}
    if(type==tri::ReverbNode){[self label:SNLocalized(@"Mix 干湿比，Size 空间大小，Damping 高频吸收，Width 立体声宽度。") y:y];y+=62;}
    if(type==tri::NoiseNode){[self label:SNLocalized(@"Type  0 白噪声  1 粉红噪声。") y:y];y+=50;}
    if(type==tri::CompressorNode){[self label:SNLocalized(@"Threshold 阈值，Ratio 压缩比，Attack/Release 包络，Makeup 补偿增益，Mix 干湿比。第二个 Audio 输入为侧链检测信号，可做闪避。") y:y];y+=74;}
    if(type==tri::LimiterNode){[self label:SNLocalized(@"Threshold 开始限幅，Ceiling 输出上限，Release 恢复速度，Gain 输入增益。") y:y];y+=62;}
    if(type==tri::ChorusNode){[self label:SNLocalized(@"Rate LFO 速度，Depth 调制深度，Mix 干湿比，Delay 基础延迟（毫秒）。") y:y];y+=62;}
    if(type==tri::QuantizeNode){[self label:SNLocalized(@"Root 调性（0=C），Scale 0 大调 1 小调 2 多利亚 3 五声 4 半音，Mode 0 最近 1 向上 2 向下。") y:y];y+=70;}
    if(type==tri::TremoloNode){[self label:SNLocalized(@"Rate 0.1–20 Hz，Depth 0–1，Mix 干湿比，Stereo 左右相位差。") y:y];y+=50;}
    if(type==tri::BitcrusherNode){[self label:SNLocalized(@"Bits 1–16 位深，Rate 500–48000 Hz 采样保持，Mix 干湿比。") y:y];y+=50;}
    if(type==tri::PhaserNode){[self label:SNLocalized(@"Rate LFO 速度，Depth 扫频深度，Feedback 反馈量，Mix 干湿比。") y:y];y+=50;}
    if(type==tri::ArpNode){[self label:SNLocalized(@"Rhythm  0 直拍  1 Bossa Nova  2 Reggae  3 Disco  4 十六分") y:y];y+=70;}
    if(type==tri::DawClockNode){[self label:SNLocalized(@"跟随 DAW 的 BPM、走带和定位。独立版可接收外部 MIDI Clock。") y:y];y+=70;}
    if(type==tri::DawOutputNode || type==tri::DawAudioNode){[self label:SNLocalized(@"在 DAW 插件中连接对应音频总线。") y:y];y+=50;}
    if(type==tri::HardwareOutputNode){[self label:SNLocalized(@"在独立版输出至系统选择的声卡。") y:y];y+=50;}
    if(type==tri::MidiOutNode){[self label:SNLocalized(@"独立版发送至 Soundnet MIDI Out；AU 使用宿主的 MIDI 输出回调。通道 0 跟随输入，1–16 强制输出通道。") y:y];y+=75;}
    _inspector.frame=NSMakeRect(0,0,298,MAX(_inspectorScroll.contentSize.height,y+20));
    [_inspector scrollPoint:sameNode?NSMakePoint(0,MIN(scrollOrigin.y,MAX(0,NSHeight(_inspector.frame)-_inspectorScroll.contentSize.height))):NSZeroPoint];
}
-(void)preset:(id)sender {
    NSTextField *field=[NSTextField new];field.stringValue=typeOf([self node:_selected])==tri::ChordNode?@"Cmaj7":@"Dm7 | G7 | Cmaj7";
    [self harmony:field];
}
// Render to a private staging directory. Cancellation never publishes partial
// audio/MIDI and never removes a destination file that already existed.
-(void)runExport:(NSInteger)kind toURL:(NSURL *)url seconds:(double)seconds rate:(double)rate bits:(int)bits {
    if(![self beginExport])return;
    auto cancelled=_exportCancellation;
    NSDictionary *document=copyJSON(_document);
    [self message:SNLocalized(@"正在渲染，可点击取消处理或停止。")];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        @autoreleasepool {
            tri::GraphSpec graph;NSString *why=nil;
            if(!triassets::decodeGraph(document,graph,&why)) {
                dispatch_async(dispatch_get_main_queue(),^{[self finishExport:why ?: SNLocalized(@"工作流无效，无法导出。")];});return;
            }
            NSURL *folder=kind==2?url:[url URLByDeletingLastPathComponent];
            NSURL *staging=[folder URLByAppendingPathComponent:[@".soundnet-export-" stringByAppendingString:NSUUID.UUID.UUIDString] isDirectory:YES];
            NSError *fileError=nil;
            if(![[NSFileManager defaultManager] createDirectoryAtURL:staging withIntermediateDirectories:NO attributes:nil error:&fileError]) {
                dispatch_async(dispatch_get_main_queue(),^{[self finishExport:fileError.localizedDescription];});return;
            }
            NSMutableArray<NSArray<NSURL *> *> *files=[NSMutableArray new];
            std::string error;BOOL ok=YES;NSString *summary=nil;
            if(kind==2) {
                std::vector<tri::offline::Stem> stems;
                ok=tri::offline::renderStems(graph,seconds,rate,stems,error,cancelled.get());
                if(ok)for(const auto& stem:stems) {
                    if(cancelled->load()){ok=NO;break;}
                    NSString *name=[NSString stringWithFormat:@"stem-%llu.wav",(unsigned long long)stem.id];
                    NSURL *temporary=[staging URLByAppendingPathComponent:name];
                    ok=tri::wav::write(temporary.path.UTF8String,stem.result.left,stem.result.right,stem.result.rate,error,bits);
                    if(!ok)break;
                    [files addObject:@[temporary,[url URLByAppendingPathComponent:name]]];
                }
                summary=[NSString stringWithFormat:SNLocalized(@"已导出 %lu 条分轨 · %.1f 秒 · %.1f kHz / %d-bit"),(unsigned long)stems.size(),seconds,rate/1000.,bits];
            } else {
                tri::offline::Result rendered;
                ok=tri::offline::render(graph,seconds,rate,rendered,error,cancelled.get());
                NSURL *temporary=[staging URLByAppendingPathComponent:url.lastPathComponent];
                if(ok && !cancelled->load()) {
                    if(kind==0) {
                        std::vector<tri::smf::TimedEvent> events;events.reserve(rendered.midi.size());
                        for(const auto& e:rendered.midi)events.push_back({e.seconds,e.status,e.a,e.b});
                        ok=tri::smf::writeMulti(temporary.path.UTF8String,events,seconds,rendered.bpm,error);
                        summary=[NSString stringWithFormat:SNLocalized(@"已导出 %lu 个 MIDI 事件 · %.1f 秒"),(unsigned long)events.size(),seconds];
                    } else {
                        ok=tri::wav::write(temporary.path.UTF8String,rendered.left,rendered.right,rendered.rate,error,bits);
                        double db=rendered.peak>0?20*std::log10(rendered.peak):-120;
                        summary=[NSString stringWithFormat:SNLocalized(@"已导出 %.1f 秒 WAV · %.1f kHz / %d-bit · 峰值 %.1f dBFS"),seconds,rate/1000.,bits,db];
                    }
                    if(ok)[files addObject:@[temporary,url]];
                }
            }
            NSString *failure=[NSString stringWithUTF8String:error.c_str()];
            dispatch_async(dispatch_get_main_queue(),^{
                NSString *message=summary;
                if(cancelled->load())message=SNLocalized(@"已取消处理，未写入导出文件。");
                else if(!ok)message=SNLocalized(failure);
                else {
                    // Serialized with UI cancellation: publication is one short
                    // main-queue phase after all expensive work has finished.
                    for(NSArray<NSURL *> *pair in files) {
                        if(std::rename(pair[0].fileSystemRepresentation,pair[1].fileSystemRepresentation)!=0) {
                            message=SNLocalized(@"无法写入导出文件，请检查目标文件夹权限。");break;
                        }
                    }
                }
                [[NSFileManager defaultManager] removeItemAtURL:staging error:nil];
                [self finishExport:message];
            });
        }
    });
}
-(void)exportMidi:(id)sender {
    if(_exportBusy){[self message:SNLocalized(@"已有处理正在进行，请等待或取消。")];return;}
    NSSavePanel *panel=[NSSavePanel savePanel];panel.nameFieldStringValue=@"Soundnet.mid";
    panel.allowedContentTypes=@[[UTType typeWithFilenameExtension:@"mid"]];
    NSView *accessory=[[NSView alloc]initWithFrame:NSMakeRect(0,0,280,52)];
    NSTextField *label=[NSTextField labelWithString:SNLocalized(@"导出时长（秒，1–120）")];
    label.frame=NSMakeRect(12,28,170,20);[accessory addSubview:label];
    NSTextField *secondsField=[[NSTextField alloc]initWithFrame:NSMakeRect(190,24,70,26)];
    secondsField.stringValue=@"8";[accessory addSubview:secondsField];
    panel.accessoryView=accessory;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result!=NSModalResponseOK)return;
        double seconds=std::max(1.0,std::min(120.0,secondsField.doubleValue));
        [self runExport:0 toURL:panel.URL seconds:seconds rate:48000 bits:16];
    }];
}

-(void)exportWav:(id)sender {
    if(_exportBusy){[self message:SNLocalized(@"已有处理正在进行，请等待或取消。")];return;}
    NSSavePanel *panel=[NSSavePanel savePanel];panel.nameFieldStringValue=@"Soundnet.wav";
    panel.allowedContentTypes=@[[UTType typeWithFilenameExtension:@"wav"]];
    NSView *accessory=[[NSView alloc]initWithFrame:NSMakeRect(0,0,300,88)];
    NSTextField *label=[NSTextField labelWithString:SNLocalized(@"导出时长（秒，1–120）")];
    label.frame=NSMakeRect(12,60,170,20);[accessory addSubview:label];
    NSTextField *secondsField=[[NSTextField alloc]initWithFrame:NSMakeRect(190,56,80,26)];
    secondsField.stringValue=@"8";[accessory addSubview:secondsField];
    NSTextField *rateLabel=[NSTextField labelWithString:SNLocalized(@"采样率")];
    rateLabel.frame=NSMakeRect(12,30,60,20);[accessory addSubview:rateLabel];
    NSPopUpButton *ratePopup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(76,26,90,26)];
    [ratePopup addItemsWithTitles:@[@"44.1 kHz",@"48 kHz",@"96 kHz"]];[ratePopup selectItemAtIndex:1];[accessory addSubview:ratePopup];
    NSTextField *bitsLabel=[NSTextField labelWithString:SNLocalized(@"位深")];
    bitsLabel.frame=NSMakeRect(180,30,40,20);[accessory addSubview:bitsLabel];
    NSPopUpButton *bitsPopup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(222,26,70,26)];
    [bitsPopup addItemsWithTitles:@[@"16-bit",@"24-bit",@"32-bit float"]];[bitsPopup selectItemAtIndex:0];[accessory addSubview:bitsPopup];
    panel.accessoryView=accessory;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result!=NSModalResponseOK)return;
        double seconds=std::max(1.0,std::min(120.0,secondsField.doubleValue));
        const double rates[]={44100,48000,96000};
        double rate=rates[std::max(0,std::min(2,(int)ratePopup.indexOfSelectedItem))];
        NSNumber *bitValue=@[@16,@24,@32][bitsPopup.indexOfSelectedItem];int bits=bitValue.intValue;
        [self runExport:1 toURL:panel.URL seconds:seconds rate:rate bits:bits];
    }];
}

-(void)exportStems:(id)sender {
    if(_exportBusy){[self message:SNLocalized(@"已有处理正在进行，请等待或取消。")];return;}
    NSOpenPanel *panel=[NSOpenPanel openPanel];panel.canChooseDirectories=YES;panel.canChooseFiles=NO;
    panel.canCreateDirectories=YES;panel.prompt=SNLocalized(@"选择文件夹");panel.message=SNLocalized(@"选择分轨导出文件夹");
    NSView *accessory=[[NSView alloc]initWithFrame:NSMakeRect(0,0,300,88)];
    NSTextField *label=[NSTextField labelWithString:SNLocalized(@"导出时长（秒，1–120）")];
    label.frame=NSMakeRect(12,60,170,20);[accessory addSubview:label];
    NSTextField *secondsField=[[NSTextField alloc]initWithFrame:NSMakeRect(190,56,80,26)];
    secondsField.stringValue=@"8";[accessory addSubview:secondsField];
    NSTextField *rateLabel=[NSTextField labelWithString:SNLocalized(@"采样率")];
    rateLabel.frame=NSMakeRect(12,30,60,20);[accessory addSubview:rateLabel];
    NSPopUpButton *ratePopup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(76,26,90,26)];
    [ratePopup addItemsWithTitles:@[@"44.1 kHz",@"48 kHz",@"96 kHz"]];[ratePopup selectItemAtIndex:1];[accessory addSubview:ratePopup];
    NSTextField *bitsLabel=[NSTextField labelWithString:SNLocalized(@"位深")];
    bitsLabel.frame=NSMakeRect(180,30,40,20);[accessory addSubview:bitsLabel];
    NSPopUpButton *bitsPopup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(222,26,70,26)];
    [bitsPopup addItemsWithTitles:@[@"16-bit",@"24-bit",@"32-bit float"]];[bitsPopup selectItemAtIndex:0];[accessory addSubview:bitsPopup];
    panel.accessoryView=accessory;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result!=NSModalResponseOK)return;
        double seconds=std::max(1.0,std::min(120.0,secondsField.doubleValue));
        const double rates[]={44100,48000,96000};
        double rate=rates[std::max(0,std::min(2,(int)ratePopup.indexOfSelectedItem))];
        NSNumber *bitValue=@[@16,@24,@32][bitsPopup.indexOfSelectedItem];int bits=bitValue.intValue;
        [self runExport:2 toURL:panel.URL seconds:seconds rate:rate bits:bits];
    }];
}

-(void)applyPreset:(NSPopUpButton *)popup {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    auto presets=presetsFor(typeOf(node));
    NSInteger index=popup.indexOfSelectedItem-1;
    if(index<0 || index>=(NSInteger)presets.size())return;
    NSDictionary *previous=copyJSON(_document);
    for(const auto& item:presets[index].values)
        setParam(node,item.first,item.second);
    [self commit:previous];[self inspect];
    [self message:[NSString stringWithFormat:SNLocalized(@"已应用预设：%@"),SNLocalized(@(presets[index].name))]];
}
-(void)resetParams:(id)sender {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    NSDictionary *previous=copyJSON(_document);
    for(int p:controls(typeOf(node)))
        setParam(node,p,tri::graphParamInfo(p).def);
    [self commit:previous];[self inspect];[self message:SNLocalized(@"已恢复默认参数。")];
}
-(void)save:(id)sender {
    NSSavePanel *panel=[NSSavePanel savePanel];panel.nameFieldStringValue=@"Soundnet.json";
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result==NSModalResponseOK)[[NSJSONSerialization dataWithJSONObject:self.document options:NSJSONWritingPrettyPrinted error:nil] writeToURL:panel.URL atomically:YES];
    }];
}
-(BOOL)adoptFile:(NSDictionary *)file {
    if(![file isKindOfClass:NSDictionary.class]) {[self message:SNLocalized(@"无效的工作流。")];return NO;}
    NSString *why=nil;
    NSDictionary *document=nil;
    // A graph document (v3 or v4) goes through the canonicaliser; anything else
    // is the old v1/v2 patch format and needs the legacy migrator.
    id version=file[@"version"];
    if([version isKindOfClass:NSNumber.class] && ([version intValue]==3 || [version intValue]==4))
        document=triassets::canonicalGraph(file,&why);
    else
        document=[self migrate:file];
    tri::GraphSpec graph;
    if(!document || !triassets::decodeGraph(document,graph,&why,true,true)) {
        [self message:why ?: SNLocalized(@"无效的工作流。")];return NO;
    }
    NSDictionary *previous=copyJSON(self.document);
    self.document=copyJSON(document);
    [self.selection removeAllObjects];self.selected=0;self.selectedEdge=-1;
    [self commit:previous];[self fit:nil];[self inspect];
    return YES;
}
-(void)open:(id)sender {
    NSOpenPanel *panel=[NSOpenPanel openPanel];panel.allowedContentTypes=@[UTTypeJSON];
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result!=NSModalResponseOK)return;
        NSDictionary *file=[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfURL:panel.URL] options:NSJSONReadingMutableContainers error:nil];
        [self adoptFile:file];
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
    NSDictionary *legacy=@{@"version":@3,@"nodes":nodes,@"edges":edges};
    return triassets::canonicalGraph(legacy,&why);
}
-(NSPoint)world:(NSPoint)p{return NSMakePoint((p.x-_origin.x)/_zoom,(p.y-_origin.y)/_zoom);}
-(void)setFrameSize:(NSSize)size {[super setFrameSize:size];[self layoutUI];}
-(void)layoutUI {
    const CGFloat W=self.bounds.size.width,H=self.bounds.size.height;
    if(W<200||H<200)return;
    const CGFloat top=98,statusH=28,inspectW=300,pianoH=_keyboardVisible?84:0;
    const CGFloat statusY=H-10-pianoH-(pianoH?6:0)-statusH;
    const CGFloat contentH=MAX(240,statusY-8-top);
    const CGFloat libW=_libraryVisible?218:0,canvasX=_libraryVisible?12+libW+10:12;
    const CGFloat canvasW=MAX(240,W-12-inspectW-10-canvasX);
    _heading.frame=NSMakeRect(16,15,176,28);
    CGFloat x=208;
    for(NSButton *button in _fileButtons){button.frame=NSMakeRect(x,12,70,30);x+=76;}
    x+=14;_playButton.frame=NSMakeRect(x,12,80,30);x+=86;
    _stopButton.frame=NSMakeRect(x,12,80,30);x+=96;
    _processingLabel.frame=NSMakeRect(x,19,140,22);
    _languagePopup.frame=NSMakeRect(W-122,12,108,30);
    _libraryToggle.frame=NSMakeRect(12,57,112,28);x=138;
    for(NSButton *button in _editButtons){
        button.frame=NSMakeRect(x,57,76,28);x+=82;
        if(button==_editButtons[1]||button==_editButtons[3])x+=12;
    }
    _libraryScroll.hidden=!_libraryVisible;
    _libraryScroll.frame=NSMakeRect(12,top,libW,contentH);
    if(_libraryVisible)[_library setFrameSize:NSMakeSize(libW-2,contentH)];
    _canvas.frame=NSMakeRect(canvasX,top,canvasW,contentH);
    _inspectorScroll.frame=NSMakeRect(W-12-inspectW,top,inspectW,contentH);
    [_inspector setFrameSize:NSMakeSize(inspectW-2,MAX(contentH,_inspector.frame.size.height))];
    CGFloat keyboardW=SNIsEnglish()?132:104;
    _keyboardToggle.frame=NSMakeRect(W-12-keyboardW,statusY,keyboardW,statusH);
    _cancelButton.frame=NSMakeRect(W-12-keyboardW-126,statusY,116,statusH);
    _exportProgress.frame=NSMakeRect(W-12-keyboardW-151,statusY+5,18,18);
    _status.frame=NSMakeRect(16,statusY+4,MAX(120,W-keyboardW-40-(_exportBusy?154:0)),statusH);
    if(_piano){_piano.hidden=!_keyboardVisible;_piano.frame=NSMakeRect(12,H-8-pianoH,MAX(120,W-24),pianoH);}
    x=canvasX+10;
    for(NSView *control in _zoomControls) {
        NSRect frame=control.frame;frame.origin=NSMakePoint(x,top+contentH-34);
        control.frame=frame;x+=frame.size.width+4;
    }
}
-(void)updateZoomLabel {_zoomLabel.stringValue=[NSString stringWithFormat:@"%ld%%",(long)std::lround(_zoom*100)];}
-(void)toggleKeyboard:(id)sender {
    _keyboardVisible=!_keyboardVisible;
    _keyboardToggle.state=_keyboardVisible?NSControlStateValueOn:NSControlStateValueOff;
    [self layoutUI];
    [self message:_keyboardVisible?SNLocalized(@"显示钢琴键盘"):SNLocalized(@"隐藏钢琴键盘")];
}
-(void)zoomBy:(double)factor atViewPoint:(NSPoint)point {
    double next=std::clamp(_zoom*factor,.15,3.0);
    if(std::abs(next-_zoom)<1e-6)return;
    // Keep the graph point under the cursor pinned while scaling.
    NSPoint anchor=[self world:point];
    _zoom=next;
    _origin=NSMakePoint(point.x-anchor.x*_zoom,point.y-anchor.y*_zoom);
    [self updateZoomLabel];_canvas.needsDisplay=YES;
}
-(void)zoomIn:(id)sender {
    NSSize size=_canvas.bounds.size;
    [self zoomBy:1.2 atViewPoint:NSMakePoint(size.width/2,size.height/2)];
}
-(void)zoomOut:(id)sender {
    NSSize size=_canvas.bounds.size;
    [self zoomBy:1/1.2 atViewPoint:NSMakePoint(size.width/2,size.height/2)];
}
-(void)zoomReset:(id)sender {
    NSSize size=_canvas.bounds.size;
    [self zoomBy:1.0/_zoom atViewPoint:NSMakePoint(size.width/2,size.height/2)];
    [self message:SNLocalized(@"缩放 100%")];
}
-(uint64_t)insertNodeType:(int)type atPoint:(NSPoint)point {
    NSDictionary *previous=copyJSON(_document);
    uint64_t next=1;
    for(NSDictionary *node in _document[@"nodes"])next=std::max(next,[node[@"id"] unsignedLongLongValue]+1);
    [self makeNode:type at:point id:next];
    _selected=next;
    [_selection removeAllObjects];[_selection addObject:@(next)];
    [self commit:previous];[self inspect];
    return next;
}
-(void)insertFromLibrary:(int)type {
    NSSize size=_canvas.bounds.size;
    NSPoint center=[self world:NSMakePoint(size.width/2-86,size.height/2-50)];
    [self insertNodeType:type atPoint:center];
    [self message:[NSString stringWithFormat:SNLocalized(@"已添加 %@"),title(type)]];
}
-(NSMutableArray *)groups {
    if(!_document)return [NSMutableArray array];
    NSMutableArray *list=_document[@"groups"];
    if(![list isKindOfClass:NSMutableArray.class]) {list=[NSMutableArray new];_document[@"groups"]=list;}
    return list;
}
// Drawing and hit testing must not insert the key: a document that differs from
// the host copy would be replaced by the one-second sync in -tick.
-(NSArray *)groupList {
    id list=_document[@"groups"];
    return [list isKindOfClass:NSArray.class]?list:@[];
}
-(void)groupSelection:(id)sender {
    NSArray<NSNumber *> *ids=[self activeIDs];
    if(!ids.count){NSBeep();[self message:SNLocalized(@"先选中要编组的节点")];return;}
    NSRect bounds=NSZeroRect;
    for(NSNumber *key in ids) {
        NSDictionary *node=[self node:key.unsignedLongLongValue];if(!node)continue;
        NSRect r=nodeRect(node);
        bounds=NSIsEmptyRect(bounds)?r:NSUnionRect(bounds,r);
    }
    if(NSIsEmptyRect(bounds))return;
    NSDictionary *previous=copyJSON(_document);
    uint64_t next=1;
    for(NSDictionary *group in [self groups])next=std::max(next,[group[@"id"] unsignedLongLongValue]+1);
    [[self groups] addObject:[@{@"id":@(next),@"title":SNLocalized(@"分组"),
        @"x":@(bounds.origin.x-18),@"y":@(bounds.origin.y-44),
        @"w":@(bounds.size.width+36),@"h":@(bounds.size.height+62)} mutableCopy]];
    [self commit:previous];_canvas.needsDisplay=YES;
    [self message:SNLocalized(@"已创建分组：框内节点会随标题一起移动 · 双击标题重命名，右键解散")];
}
-(void)dissolveGroup:(id)sender {
    NSInteger tag=[sender respondsToSelector:@selector(tag)]?[sender tag]:0;
    uint64_t target=tag>0?(uint64_t)tag:_groupDrag;
    if(!target)return;
    NSDictionary *previous=copyJSON(_document);
    NSIndexSet *indexes=[[self groups] indexesOfObjectsPassingTest:^BOOL(NSDictionary *group,NSUInteger,BOOL*){
        return [group[@"id"] unsignedLongLongValue]==target;
    }];
    if(!indexes.count)return;
    [[self groups] removeObjectsAtIndexes:indexes];
    _groupDrag=0;
    [self commit:previous];_canvas.needsDisplay=YES;
    [self message:SNLocalized(@"已解散分组（节点保留）")];
}
-(void)renameGroup:(NSDictionary *)group {
    NSAlert *alert=[NSAlert new];
    alert.messageText=SNLocalized(@"分组名称");
    alert.informativeText=SNLocalized(@"留空则显示为「分组」。");
    NSTextField *field=[[NSTextField alloc]initWithFrame:NSMakeRect(0,0,240,24)];
    field.stringValue=group[@"title"] ?: SNLocalized(@"分组");
    alert.accessoryView=field;
    [alert addButtonWithTitle:SNLocalized(@"确定")];[alert addButtonWithTitle:SNLocalized(@"取消")];
    NSDictionary *previous=copyJSON(_document);
    NSNumber *gid=group[@"id"];
    __weak SNInstanceEditor *weakSelf=self;
    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response){
        if(response!=NSAlertFirstButtonReturn)return;
        SNInstanceEditor *editor=weakSelf;if(!editor)return;
        NSString *name=field.stringValue;
        for(NSMutableDictionary *entry in [editor groups])
            if([entry[@"id"] isEqual:gid]) {entry[@"title"]=name.length?name:SNLocalized(@"分组");break;}
        [editor commit:previous];editor.canvas.needsDisplay=YES;
    }];
}
-(void)renameGroupMenu:(NSMenuItem *)sender {if([sender.representedObject isKindOfClass:NSDictionary.class])[self renameGroup:sender.representedObject];}
-(NSDictionary *)groupAtPoint:(NSPoint)point titleOnly:(BOOL)titleOnly {
    for(NSDictionary *group in [[self groupList] reverseObjectEnumerator]) {
        NSRect r=NSMakeRect([group[@"x"] doubleValue],[group[@"y"] doubleValue],
                            [group[@"w"] doubleValue],[group[@"h"] doubleValue]);
        if(titleOnly)r.size.height=28;
        if(NSPointInRect(point,r))return group;
    }
    return nil;
}
-(void)commitGroups:(NSDictionary *)previous {[self commit:previous];_canvas.needsDisplay=YES;}
-(void)showShortcuts:(id)sender {
    NSAlert *alert=[NSAlert new];
    alert.messageText=SNLocalized(@"Soundnet 快捷键");
    alert.informativeText=SNLocalized(@"文件与历史\n⌘O 打开  ⌘S 保存  ⌘Z 撤销  ⇧⌘Z 重做\n\n编辑\n⌘C 复制  ⌘V 粘贴  ⌘X 剪切  ⌘A 全选  ⌘D 复制节点  ⌘F 查找\nDelete 删除节点或连线  Esc 取消选择  方向键微移（⇧ 10 像素）\n⇧⌘G 把选中节点编组（双击分组标题重命名）\n\n画布\n拖动空白框选  拖动分组标题整体移动  Option 拖动或中键拖动平移  滚轮平移\n⌘滚轮或触控板捏合缩放（以光标为中心）\n左侧节点库：单击插入到画布中央，直接拖到画布任意位置\n\n触发\n工具条或音频菜单：播放启动全部开始触发器，停止暂停全图处理；⌘. 停止播放与处理");
    [alert beginSheetModalForWindow:self.window completionHandler:nil];
}
-(void)fit:(id)sender {
    NSRect all=NSZeroRect;
    for(NSDictionary *node in _document[@"nodes"]) {
        NSRect r=nodeRect(node);
        all=NSIsEmptyRect(all)?r:NSUnionRect(all,r);
    }
    for(NSDictionary *group in [self groupList]) {
        NSRect r=NSMakeRect([group[@"x"] doubleValue],[group[@"y"] doubleValue],
                            [group[@"w"] doubleValue],[group[@"h"] doubleValue]);
        all=NSIsEmptyRect(all)?r:NSUnionRect(all,r);
    }
    if(NSIsEmptyRect(all))all=NSMakeRect(0,0,700,400);
    NSSize size=_canvas.bounds.size;
    double availableW=std::max(200.,size.width-48),availableH=std::max(160.,size.height-48);
    _zoom=std::clamp(std::min(availableW/all.size.width,availableH/all.size.height),.1,1.2);
    _origin=NSMakePoint((size.width-all.size.width*_zoom)/2-all.origin.x*_zoom,
                        (size.height-all.size.height*_zoom)/2-all.origin.y*_zoom);
    [self updateZoomLabel];_canvas.needsDisplay=YES;
}
-(void)autoLayout:(id)sender {
    NSArray<NSDictionary*> *nodes=_document[@"nodes"];
    if(!nodes.count)return;
    NSMutableDictionary<NSNumber*,NSNumber*> *depth=[NSMutableDictionary new];
    for(NSDictionary *node in nodes)depth[node[@"id"]]=@0;
    NSArray<NSDictionary*> *edges=_document[@"edges"];
    for(NSUInteger pass=0;pass<nodes.count;++pass) {
        BOOL changed=NO;
        for(NSDictionary *edge in edges) {
            NSNumber *source=edge[@"source"],*target=edge[@"target"];
            if(!depth[source] || !depth[target])continue;
            NSUInteger next=[depth[source] unsignedIntegerValue]+1;
            if(next>[depth[target] unsignedIntegerValue]){depth[target]=@(next);changed=YES;}
        }
        if(!changed)break;
    }
    NSMutableDictionary<NSNumber*,NSNumber*> *slots=[NSMutableDictionary new];
    NSArray<NSDictionary*> *ordered=[nodes sortedArrayUsingComparator:^NSComparisonResult(NSDictionary *a,NSDictionary *b){
        return [a[@"id"] compare:b[@"id"]];
    }];
    NSDictionary *previous=copyJSON(_document);
    for(NSDictionary *node in ordered) {
        NSNumber *level=depth[node[@"id"]] ?: @0;
        NSUInteger slot=[slots[level] unsignedIntegerValue];slots[level]=@(slot+1);
        NSMutableDictionary *mutableNode=(NSMutableDictionary *)node;
        mutableNode[@"x"]=@(80+[level doubleValue]*240);
        mutableNode[@"y"]=@(60+slot*150);
    }
    [self commit:previous];[self fit:nil];_canvas.needsDisplay=YES;
    [self message:[NSString stringWithFormat:SNLocalized(@"已按信号深度排列 %lu 个节点"),(unsigned long)nodes.count]];
}
-(void)blank:(id)sender {
    NSDictionary *previous=copyJSON(_document);
    _document=[@{@"version":@4,@"projectId":@"local",@"nodes":[NSMutableArray new],@"edges":[NSMutableArray new]}mutableCopy];
    _selected=0;[self commit:previous];[self inspect];
}
-(void)tick {
    [self updateProcessingControls];
    NSDictionary *remote=_assetsRead ? _assetsRead()[@"graph"] : nil;
    if(remote && ![remote isEqual:_document] && !_moving && !_wireSource) {
        NSString *reason=nil;
        NSDictionary *canonical=triassets::canonicalGraph(remote,&reason);
        if(!canonical)return;
        remote=canonical;
        if([remote isEqual:_document])return;
        _document=copyJSON(remote);[self pruneSelection];[self inspect];_canvas.needsDisplay=YES;
    }
}
@end

namespace {
NSRect nodeRect(NSDictionary *node) {
    int type=typeOf(node);
    int count=std::max((int)tri::inputTypes(type).size(),tri::outputCount(type));
    return NSMakeRect([node[@"x"] doubleValue],[node[@"y"] doubleValue],172,std::max(88,48+count*24));
}
NSPoint jack(NSDictionary *node,bool out,int port) {
    NSRect r=nodeRect(node);
    return NSMakePoint(out?NSMaxX(r):NSMinX(r),NSMinY(r)+47+24*port);
}
double curveX(NSPoint a,NSPoint c1,NSPoint c2,NSPoint b,double t) {
    double u=1-t;return u*u*u*a.x+3*u*u*t*c1.x+3*u*t*t*c2.x+t*t*t*b.x;
}
double curveY(NSPoint a,NSPoint c1,NSPoint c2,NSPoint b,double t) {
    double u=1-t;return u*u*u*a.y+3*u*u*t*c1.y+3*u*t*t*c2.y+t*t*t*b.y;
}
bool nearEdge(NSDictionary *edge,NSDictionary *a,NSDictionary *b,NSPoint point,double tolerance) {
    if(!a || !b)return false;
    int port=inputPortOf(b,edge),out=outputPortOf(a,edge);
    NSPoint from=jack(a,true,out),to=jack(b,false,port);
    NSPoint c1=NSMakePoint(from.x+70,from.y),c2=NSMakePoint(to.x-70,to.y);
    for(int i=0;i<=32;++i) {
        double t=i/32.0;
        if(std::hypot(curveX(from,c1,c2,to,t)-point.x,curveY(from,c1,c2,to,t)-point.y)<tolerance)return true;
    }
    return false;
}
void drawText(NSString *text,NSRect rect,NSColor *color,double size=12) {
    [text drawInRect:rect withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:size],
        NSForegroundColorAttributeName:color}];
}
}
@implementation SNInstanceCanvas
-(instancetype)initWithFrame:(NSRect)frame {
    if((self=[super initWithFrame:frame]))
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL,SNNodeTypePasteboard]];
    return self;
}
-(BOOL)isFlipped{return YES;}
-(NSArray<NSURL *> *)draggedURLs:(id<NSDraggingInfo>)sender {
    return [sender.draggingPasteboard readObjectsForClasses:@[NSURL.class]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
}
-(NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    if([sender.draggingPasteboard stringForType:SNNodeTypePasteboard])return NSDragOperationCopy;
    return [self draggedURLs:sender].count?NSDragOperationCopy:NSDragOperationNone;
}
-(NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender { return [self draggingEntered:sender]; }
-(BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSString *nodeType=[sender.draggingPasteboard stringForType:SNNodeTypePasteboard];
    NSPoint view=[self convertPoint:sender.draggingLocation fromView:nil];
    if(nodeType) {
        int type=nodeType.intValue;
        if(!tri::nodeMeta(type))return NO;
        NSPoint at=[_editor world:view];
        [_editor insertNodeType:type atPoint:NSMakePoint(at.x-86,at.y-40)];
        return YES;
    }
    NSArray<NSURL *> *urls=[self draggedURLs:sender];
    if(!urls.count)return NO;
    BOOL clipMode=([NSEvent modifierFlags]&NSEventModifierFlagOption)!=0;
    return [_editor dropFileURLs:urls atPoint:[_editor world:view] clipMode:clipMode];
}
-(BOOL)acceptsFirstResponder{return YES;}
-(void)drawRect:(NSRect)dirty {
    [[NSColor colorWithWhite:.09 alpha:1]setFill];NSRectFill(self.bounds);
    SNInstanceEditor *editor=_editor;
    [NSGraphicsContext saveGraphicsState];
    NSBezierPath *clip=[NSBezierPath bezierPathWithRect:self.bounds];[clip addClip];
    NSAffineTransform *transform=[NSAffineTransform transform];
    [transform translateXBy:editor.origin.x yBy:editor.origin.y];[transform scaleBy:editor.zoom];[transform concat];
    if(editor.snap) {
        NSPoint a=[editor world:NSMakePoint(NSMinX(self.bounds),NSMinY(self.bounds))];
        NSPoint b=[editor world:NSMakePoint(NSMaxX(self.bounds),NSMaxY(self.bounds))];
        double x0=std::floor(std::min(a.x,b.x)/20)*20,x1=std::max(a.x,b.x),y0=std::floor(std::min(a.y,b.y)/20)*20,y1=std::max(a.y,b.y);
        [[NSColor colorWithWhite:.14 alpha:1]setStroke];
        NSBezierPath *grid=[NSBezierPath new];grid.lineWidth=1/[editor zoom];
        for(double x=x0;x<=x1;x+=20){[grid moveToPoint:NSMakePoint(x,a.y)];[grid lineToPoint:NSMakePoint(x,b.y)];}
        for(double y=y0;y<=y1;y+=20){[grid moveToPoint:NSMakePoint(a.x,y)];[grid lineToPoint:NSMakePoint(b.x,y)];}
        [grid stroke];
    }
    for(NSDictionary *group in [editor groupList]) {
        NSRect r=NSMakeRect([group[@"x"] doubleValue],[group[@"y"] doubleValue],
                            [group[@"w"] doubleValue],[group[@"h"] doubleValue]);
        [[NSColor colorWithWhite:1 alpha:.045]setFill];
        [[NSBezierPath bezierPathWithRoundedRect:r xRadius:9 yRadius:9]fill];
        NSBezierPath *border=[NSBezierPath bezierPathWithRoundedRect:r xRadius:9 yRadius:9];
        [border setLineDash:(CGFloat[]){6,4} count:2 phase:0];
        border.lineWidth=1/[editor zoom];[[NSColor colorWithWhite:1 alpha:.18]setStroke];[border stroke];
        NSRect title=NSMakeRect(r.origin.x,r.origin.y,r.size.width,28);
        // NSRectFill composites with Copy and would drop the alpha, so the
        // translucent header has to go through a bezier path.
        [[NSColor colorWithWhite:1 alpha:.08]setFill];
        [[NSBezierPath bezierPathWithRect:title]fill];
        [[NSColor colorWithWhite:1 alpha:.12]setFill];
        [[NSBezierPath bezierPathWithRect:NSMakeRect(r.origin.x,r.origin.y+27,r.size.width,1)]fill];
        drawText(group[@"title"] ?: SNLocalized(@"分组"),NSInsetRect(title,10,6),[NSColor colorWithWhite:.75 alpha:1],11);
    }
    NSInteger edgeIndex=0;
    for(NSDictionary *edge in editor.document[@"edges"]) {
        NSDictionary *a=[editor node:[edge[@"source"] unsignedLongLongValue]],*b=[editor node:[edge[@"target"] unsignedLongLongValue]];
        int port=inputPortOf(b,edge),out=outputPortOf(a,edge);
        NSPoint from=jack(a,true,out),to=jack(b,false,port);
        NSBezierPath *path=[NSBezierPath new];[path moveToPoint:from];
        [path curveToPoint:to controlPoint1:NSMakePoint(from.x+70,from.y) controlPoint2:NSMakePoint(to.x-70,to.y)];
        BOOL chosen=(edgeIndex==editor.selectedEdge);
        [chosen?NSColor.whiteColor:signalColor(tri::outputType(typeOf(a),out)) setStroke];
        path.lineWidth=chosen?3:2;[path stroke];
        ++edgeIndex;
    }
    for(NSDictionary *node in editor.document[@"nodes"]) {
        int type=typeOf(node);NSRect r=nodeRect(node);BOOL bypassed=[node[@"bypass"] boolValue];
        [[NSColor colorWithWhite:bypassed?.13:.18 alpha:1]setFill];[[NSBezierPath bezierPathWithRoundedRect:r xRadius:4 yRadius:4]fill];
        [[editor isSelected:[node[@"id"] unsignedLongLongValue]]?NSColor.whiteColor:[NSColor colorWithWhite:.4 alpha:1] setStroke];
        [[NSBezierPath bezierPathWithRect:r]stroke];
        drawText([NSString stringWithFormat:@"%@ #%@",nodeLabel(node),node[@"id"]],NSMakeRect(r.origin.x+9,r.origin.y+8,160,28),NSColor.whiteColor);
        if(bypassed)drawText(SNLocalized(@"旁通"),NSMakeRect(NSMaxX(r)-46,r.origin.y+8,40,20),NSColor.systemOrangeColor,10);
        if(type==0) {
            [[NSColor systemRedColor]setStroke];
            NSBezierPath *warn=[NSBezierPath bezierPathWithRect:r];[warn stroke];
            drawText(node[@"nodeKey"] ?: SNLocalized(@"未知类型"),NSMakeRect(r.origin.x+9,r.origin.y+30,150,18),
                     NSColor.systemRedColor,10);
        }
        auto inputs=tri::inputTypes(type);
        for(int p=0;p<(int)inputs.size();++p) {
            NSPoint at=jack(node,false,p);[signalColor(inputs[p])setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(at.x-5,at.y-5,10,10)]fill];
            drawText(portLabel(type,true,p),NSMakeRect(at.x+9,at.y-8,55,20),NSColor.lightGrayColor);
        }
        int outputs=tri::outputCount(type);
        for(int p=0;p<outputs;++p) {
            NSPoint at=jack(node,true,p);[signalColor(tri::outputType(type,p))setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(at.x-5,at.y-5,10,10)]fill];
            drawText(portLabel(type,false,p),NSMakeRect(at.x-68,at.y-8,62,20),NSColor.lightGrayColor);
        }
    }
    if(editor.boxSelecting) {
        NSRect box=NSMakeRect(std::min(editor.boxOrigin.x,editor.boxCurrent.x),std::min(editor.boxOrigin.y,editor.boxCurrent.y),
                              std::fabs(editor.boxCurrent.x-editor.boxOrigin.x),std::fabs(editor.boxCurrent.y-editor.boxOrigin.y));
        NSBezierPath *path=[NSBezierPath bezierPathWithRect:box];[path setLineDash:(CGFloat[]){6,4} count:2 phase:0];
        path.lineWidth=1.5;[NSColor.whiteColor setStroke];[path stroke];
    }
    [NSGraphicsContext restoreGraphicsState];
}
-(void)rightMouseDown:(NSEvent *)event {
    SNInstanceEditor *editor=_editor;
    editor.menuPoint=[editor world:[self convertPoint:event.locationInWindow fromView:nil]];
    NSMenu *menu=[NSMenu new];
    for(NSString *group in @[SNLocalized(@"输入"),SNLocalized(@"处理"),SNLocalized(@"逻辑"),SNLocalized(@"注释"),SNLocalized(@"输出")]) {
        NSMenuItem *category=[menu addItemWithTitle:group action:nil keyEquivalent:@""];
        category.submenu=[NSMenu new];
        for(NSArray *row in catalog())if([row[2] isEqual:group]) {
            NSMenuItem *item=[category.submenu addItemWithTitle:row[1] action:@selector(insert:) keyEquivalent:@""];
            item.target=editor;item.tag=[row[0] integerValue];
        }
    }
    [menu addItem:[NSMenuItem separatorItem]];
    BOOL hasSelection=[editor activeIDs].count>0;
    for(NSArray *item in @[@[SNLocalized(@"复制到剪贴板"),@"copy:",@"c"],@[SNLocalized(@"剪切"),@"cut:",@"x"],@[SNLocalized(@"粘贴节点"),@"paste:",@"v"],
                           @[SNLocalized(@"复制并偏移"),@"duplicate:",@""],@[SNLocalized(@"断开选中节点"),@"disconnect:",@""],@[SNLocalized(@"删除选中节点"),@"remove:",@""]]) {
        NSMenuItem *action=[menu addItemWithTitle:item[0] action:NSSelectorFromString(item[1]) keyEquivalent:item[2]];
        action.target=editor;
        action.enabled=[item[1] isEqualToString:@"paste:"]?editor.clipboard!=nil:hasSelection;
    }
    [menu addItem:[NSMenuItem separatorItem]];
    NSDictionary *group=[editor groupAtPoint:editor.menuPoint titleOnly:NO];
    NSMenuItem *groupItem=[menu addItemWithTitle:SNLocalized(@"编组选中节点") action:@selector(groupSelection:) keyEquivalent:@"G"];
    groupItem.target=editor;groupItem.enabled=hasSelection;
    groupItem.keyEquivalentModifierMask=NSEventModifierFlagCommand|NSEventModifierFlagShift;
    if(group) {
        NSMenuItem *rename=[menu addItemWithTitle:SNLocalized(@"重命名分组…") action:@selector(renameGroupMenu:) keyEquivalent:@""];
        rename.target=editor;rename.representedObject=group;
        NSMenuItem *dissolve=[menu addItemWithTitle:SNLocalized(@"解散分组") action:@selector(dissolveGroup:) keyEquivalent:@""];
        dissolve.target=editor;dissolve.tag=[group[@"id"] integerValue];
    }
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *all=[menu addItemWithTitle:SNLocalized(@"选择全部") action:@selector(selectAll:) keyEquivalent:@"a"];
    all.target=editor;all.enabled=[(NSArray *)editor.document[@"nodes"] count]>0;
    [NSMenu popUpContextMenu:menu withEvent:event forView:self];
}
-(void)mouseDown:(NSEvent *)event {
    SNInstanceEditor *editor=_editor;[self.window makeFirstResponder:self];
    NSPoint view=[self convertPoint:event.locationInWindow fromView:nil],point=[editor world:view];
    editor.lastPoint=view;editor.dragStart=copyJSON(editor.document);
    BOOL additive=(event.modifierFlags&(NSEventModifierFlagShift|NSEventModifierFlagCommand))!=0;
    for(NSDictionary *node in [editor.document[@"nodes"] reverseObjectEnumerator]) {
        int type=typeOf(node);
        int outputs=tri::outputCount(type);
        for(int p=0;p<outputs;++p) {
            NSPoint pos=jack(node,true,p);
            if(std::hypot(pos.x-point.x,pos.y-point.y)<12/editor.zoom) {
                editor.wireSource=[node[@"id"] unsignedLongLongValue];editor.wirePort=p;return;
            }
        }
        if(NSPointInRect(point,nodeRect(node))) {
            uint64_t uid=[node[@"id"] unsignedLongLongValue];
            editor.moving=0;
            if(additive) {
                [editor toggleSelection:uid];
                if([editor isSelected:uid])editor.moving=uid;
            } else {
                if(![editor isSelected:uid])[editor selectOnly:uid];
                else {editor.selected=uid;[editor inspect];}
                editor.moving=uid;
            }
            self.needsDisplay=YES;return;
        }
    }
    NSInteger index=0;
    for(NSDictionary *edge in editor.document[@"edges"]) {
        NSDictionary *a=[editor node:[edge[@"source"] unsignedLongLongValue]],*b=[editor node:[edge[@"target"] unsignedLongLongValue]];
        if(nearEdge(edge,a,b,point,7/editor.zoom)) {
            [editor selectNone:nil];editor.selectedEdge=index;
            [editor message:SNLocalized(@"已选中连线，按 Delete 删除。")];
            self.needsDisplay=YES;return;
        }
        ++index;
    }
    NSDictionary *group=[editor groupAtPoint:point titleOnly:YES];
    if(group) {
        if(event.clickCount==2) {[editor renameGroup:group];return;}
        NSRect area=NSMakeRect([group[@"x"] doubleValue],[group[@"y"] doubleValue],
                               [group[@"w"] doubleValue],[group[@"h"] doubleValue]);
        editor.dragStartGroup=copyJSON(editor.document);
        editor.groupDrag=[group[@"id"] unsignedLongLongValue];
        editor.groupGrab=point;
        NSMutableDictionary *members=[NSMutableDictionary new];
        for(NSDictionary *node in editor.document[@"nodes"]) {
            NSRect r=nodeRect(node);
            if(NSPointInRect(NSMakePoint(NSMidX(r),NSMidY(r)),area))
                members[node[@"id"]]=@[@([node[@"x"] doubleValue]),@([node[@"y"] doubleValue])];
        }
        editor.groupDragNodes=members;
        [editor message:[NSString stringWithFormat:SNLocalized(@"拖动标题可整体移动 %lu 个节点"),(unsigned long)members.count]];
        return;
    }
    BOOL wantsPan=(event.modifierFlags&NSEventModifierFlagOption)!=0;
    if(!wantsPan) {
        editor.boxSelecting=YES;editor.boxOrigin=point;editor.boxCurrent=point;
        if(!additive)[editor selectNone:nil];
        self.needsDisplay=YES;return;
    }
    [editor selectNone:nil];editor.panning=YES;
}
-(void)mouseDragged:(NSEvent *)event {
    SNInstanceEditor *editor=_editor;
    NSPoint view=[self convertPoint:event.locationInWindow fromView:nil];
    if(editor.groupDrag) {
        NSPoint point=[editor world:view];
        double dx=point.x-editor.groupGrab.x,dy=point.y-editor.groupGrab.y;
        NSDictionary *base=editor.dragStartGroup;
        for(NSDictionary *origin in base[@"groups"])
            if([origin[@"id"] unsignedLongLongValue]==editor.groupDrag)
                for(NSMutableDictionary *target in [editor groups])
                    if([target[@"id"] isEqual:origin[@"id"]]) {
                        target[@"x"]=@([origin[@"x"] doubleValue]+dx);
                        target[@"y"]=@([origin[@"y"] doubleValue]+dy);
                        break;
                    }
        for(NSNumber *key in editor.groupDragNodes) {
            NSArray *origin=editor.groupDragNodes[key];
            NSMutableDictionary *node=[editor node:key.unsignedLongLongValue];if(!node)continue;
            node[@"x"]=@([origin[0] doubleValue]+dx);node[@"y"]=@([origin[1] doubleValue]+dy);
        }
        self.needsDisplay=YES;return;
    }
    if(editor.boxSelecting) {
        editor.boxCurrent=[editor world:view];self.needsDisplay=YES;return;
    }
    double dx=view.x-editor.lastPoint.x,dy=view.y-editor.lastPoint.y;
    if(editor.moving) {
        NSArray<NSNumber *> *ids=[editor activeIDs];
        for(NSNumber *key in ids) {
            NSMutableDictionary *node=[editor node:key.unsignedLongLongValue];if(!node)continue;
            double x=[node[@"x"] doubleValue]+dx/editor.zoom,y=[node[@"y"] doubleValue]+dy/editor.zoom;
            if(editor.snap){x=std::round(x/20)*20;y=std::round(y/20)*20;}
            node[@"x"]=@(x);node[@"y"]=@(y);
        }
    } else if(editor.panning)editor.origin=NSMakePoint(editor.origin.x+dx,editor.origin.y+dy);
    editor.lastPoint=view;self.needsDisplay=YES;
}
-(void)mouseUp:(NSEvent *)event {
    SNInstanceEditor *editor=_editor;
    if(editor.groupDrag) {
        [editor commitGroups:editor.dragStartGroup];
        editor.groupDrag=0;editor.dragStartGroup=nil;editor.groupDragNodes=nil;
        self.needsDisplay=YES;return;
    }
    if(editor.boxSelecting) {
        NSRect box=NSMakeRect(std::min(editor.boxOrigin.x,editor.boxCurrent.x),std::min(editor.boxOrigin.y,editor.boxCurrent.y),
                              std::fabs(editor.boxCurrent.x-editor.boxOrigin.x),std::fabs(editor.boxCurrent.y-editor.boxOrigin.y));
        for(NSDictionary *node in editor.document[@"nodes"])
            if(NSIntersectsRect(box,nodeRect(node))) {
                [editor.selection addObject:node[@"id"]];editor.selected=[node[@"id"] unsignedLongLongValue];
            }
        editor.boxSelecting=NO;editor.selectedEdge=-1;
        [editor inspect];self.needsDisplay=YES;return;
    }
    if(editor.wireSource) {
        NSPoint point=[editor world:[self convertPoint:event.locationInWindow fromView:nil]];
        for(NSDictionary *node in editor.document[@"nodes"]) {
            int targetType=typeOf(node);
            auto inputs=tri::inputTypes(targetType);
            for(int p=0;p<(int)inputs.size();++p) {
                NSPoint pos=jack(node,false,p);
                if(std::hypot(pos.x-point.x,pos.y-point.y)>14/editor.zoom)continue;
                uint64_t uid=[node[@"id"] unsignedLongLongValue];
                NSIndexSet *existing=[editor.document[@"edges"] indexesOfObjectsPassingTest:^BOOL(NSDictionary *e,NSUInteger,BOOL*){
                    return [e[@"target"] unsignedLongLongValue]==uid && inputPortOf(node,e)==p;
                }];
                if(inputs[p]!=tri::Signal::Trigger)[editor.document[@"edges"] removeObjectsAtIndexes:existing];
                NSDictionary *source=[editor node:editor.wireSource];
                [editor.document[@"edges"] addObject:makeEdge(editor.wireSource,uid,targetType,p,
                    source?typeOf(source):0,editor.wirePort)];
                [editor commit:editor.dragStart];editor.wireSource=0;editor.selectedEdge=-1;
                self.needsDisplay=YES;return;
            }
        }
    }
    if(editor.moving)[editor commit:editor.dragStart];
    editor.wireSource=0;editor.moving=0;editor.panning=NO;
    self.needsDisplay=YES;
}
-(void)scrollWheel:(NSEvent *)event {
    if(event.modifierFlags&NSEventModifierFlagCommand) {
        NSPoint point=[self convertPoint:event.locationInWindow fromView:nil];
        [_editor zoomBy:std::exp(-event.scrollingDeltaY*.02) atViewPoint:point];
    } else {
        _editor.origin=NSMakePoint(_editor.origin.x-event.scrollingDeltaX,_editor.origin.y-event.scrollingDeltaY);
    }
    self.needsDisplay=YES;
}
-(void)magnifyWithEvent:(NSEvent *)event {
    NSPoint point=[self convertPoint:event.locationInWindow fromView:nil];
    [_editor zoomBy:1+event.magnification atViewPoint:point];
    self.needsDisplay=YES;
}
-(void)keyDown:(NSEvent *)event {
    SNInstanceEditor *editor=_editor;
    if(event.modifierFlags&NSEventModifierFlagCommand) {
        NSString *key=event.charactersIgnoringModifiers.lowercaseString;
        if([key isEqualToString:@"c"]){[editor copy:nil];return;}
        if([key isEqualToString:@"v"]){[editor paste:nil];return;}
        if([key isEqualToString:@"x"]){[editor cut:nil];return;}
        if([key isEqualToString:@"a"]){[editor selectAll:nil];return;}
        if([key isEqualToString:@"d"]){[editor duplicate:nil];return;}
        if([key isEqualToString:@"f"]){[editor search:nil];return;}
        if([key isEqualToString:@"g"]){if(event.modifierFlags&NSEventModifierFlagShift)[editor groupSelection:nil];else[editor snap:nil];return;}
    }
    if(event.keyCode==51 || event.keyCode==117) {
        if(editor.selectedEdge>=0)[editor deleteEdge:editor.selectedEdge];
        else [editor remove:nil];
        return;
    }
    if(event.keyCode==53){[editor selectNone:nil];return;}
    if(event.keyCode>=123 && event.keyCode<=126) {
        double step=(event.modifierFlags&NSEventModifierFlagShift)?10:1;
        double dx=event.keyCode==123?-step:event.keyCode==124?step:0;
        double dy=event.keyCode==126?-step:event.keyCode==125?step:0;
        NSDictionary *previous=copyJSON(editor.document);
        NSArray<NSNumber *> *ids=[editor activeIDs];
        if(!ids.count)return;
        for(NSNumber *key in ids) {
            NSMutableDictionary *node=[editor node:key.unsignedLongLongValue];if(!node)continue;
            node[@"x"]=@([node[@"x"] doubleValue]+dx);node[@"y"]=@([node[@"y"] doubleValue]+dy);
        }
        [editor commit:previous];self.needsDisplay=YES;return;
    }
    [super keyDown:event];
}
@end

@interface SNFlippedView : NSView
@end
@implementation SNFlippedView
-(BOOL)isFlipped{return YES;}
@end
@interface SNPiano : NSView
@property(copy) TRIMIDI send;
@property int octave;
@end
@implementation SNPiano {
    int _active, _octaveIndex;
}
-(BOOL)isFlipped{return YES;}
-(int)lowNote { return 48+_octaveIndex*12; }
-(int)noteAtPoint:(NSPoint)p black:(BOOL *)black {
    const double w=self.bounds.size.width,h=self.bounds.size.height;
    const double whiteW=w/14.0;
    static const int boundaries[5]={1,2,4,5,6};
    static const int semitones[5]={1,3,6,8,10};
    // Black keys sit on top of the white rows, so they win the hit test.
    for(int octave=1;octave>=0;--octave) {
        for(int i=4;i>=0;--i) {
            double cx=(octave*7+boundaries[i])*whiteW;
            NSRect r=NSMakeRect(cx-whiteW*.31,0,whiteW*.62,h*.60);
            if(NSPointInRect(p,r)) {
                if(black)*black=YES;
                return [self lowNote]+octave*12+semitones[i];
            }
        }
    }
    static const int whiteSemitones[7]={0,2,4,5,7,9,11};
    int index=std::clamp(int(std::floor(p.x/std::max(whiteW,.001))),0,13);
    if(black)*black=NO;
    return [self lowNote]+(index/7)*12+whiteSemitones[index%7];
}
-(void)drawRect:(NSRect)dirty {
    const double w=self.bounds.size.width,h=self.bounds.size.height;
    const double whiteW=w/14.0;
    [[NSColor colorWithWhite:.08 alpha:1]setFill];NSRectFill(self.bounds);
    static const int whiteSemitones[7]={0,2,4,5,7,9,11};
    NSColor *pressed=[NSColor colorWithCalibratedRed:.95 green:.62 blue:.25 alpha:1];
    for(int i=0;i<14;++i) {
        int note=[self lowNote]+(i/7)*12+whiteSemitones[i%7];
        NSRect r=NSMakeRect(i*whiteW+1,0,whiteW-2,h-3);
        NSBezierPath *key=[NSBezierPath bezierPathWithRoundedRect:r xRadius:3 yRadius:3];
        [(note==_active?pressed:[NSColor colorWithCalibratedWhite:.93 alpha:1]) setFill];[key fill];
        [[NSColor colorWithWhite:.32 alpha:1]setStroke];key.lineWidth=1;[key stroke];
        if(i%7==0)drawText(pitchName(note),NSMakeRect(r.origin.x+4,h-17,whiteW-6,13),
                           [NSColor colorWithWhite:.38 alpha:1],9);
    }
    static const int boundaries[5]={1,2,4,5,6},semitones[5]={1,3,6,8,10};
    for(int octave=0;octave<2;++octave) for(int i=0;i<5;++i) {
        int note=[self lowNote]+octave*12+semitones[i];
        double cx=(octave*7+boundaries[i])*whiteW;
        NSRect r=NSMakeRect(cx-whiteW*.31,0,whiteW*.62,h*.60);
        NSBezierPath *key=[NSBezierPath bezierPathWithRoundedRect:r xRadius:2 yRadius:2];
        [(note==_active?pressed:[NSColor colorWithWhite:.12 alpha:1]) setFill];[key fill];
        [[NSColor colorWithWhite:.30 alpha:1]setStroke];key.lineWidth=1;[key stroke];
    }
}
-(void)press:(int)note {
    if(note==_active)return;
    if(_active && _send)_send(0x80,(unsigned char)_active,0);
    _active=note;
    if(_active && _send)_send(0x90,(unsigned char)_active,100);
    self.needsDisplay=YES;
}
-(void)releaseNote {
    if(_active && _send)_send(0x80,(unsigned char)_active,0);
    _active=0;self.needsDisplay=YES;
}
-(void)mouseDown:(NSEvent *)event {
    [self press:[self noteAtPoint:[self convertPoint:event.locationInWindow fromView:nil] black:nil]];
}
-(void)mouseDragged:(NSEvent *)event {
    NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];
    if(!NSPointInRect(p,self.bounds)){[self releaseNote];return;}
    [self press:[self noteAtPoint:p black:nil]];
}
-(void)mouseUp:(NSEvent *)event {[self releaseNote];}
-(void)viewDidMoveToWindow {if(!self.window)[self releaseNote];}
@end

// Categorised node palette. Rows are built from the same registry the canvas
// context menu uses, so a new node type shows up in both places.
@interface SNNodeLibrary : NSView <NSDraggingSource, NSSearchFieldDelegate>
@property(weak) SNInstanceEditor *editor;
- (void)rebuild;
- (void)setSearchQuery:(NSString *)query;
- (void)toggleCategory:(NSString *)category;
@end
@implementation SNNodeLibrary {
    NSMutableArray<NSDictionary *> *_rows;
    NSMutableSet<NSString *> *_collapsedCategories;
    NSSearchField *_searchField;
    NSButton *_expandButton, *_collapseButton;
    NSInteger _hover, _pressed;
    NSUInteger _matchCount;
    NSPoint _pressPoint;
    BOOL _dragged;
    double _contentHeight;
}
-(instancetype)initWithFrame:(NSRect)frame {
    if((self=[super initWithFrame:frame])) {
        _hover=-1;_pressed=-1;
        NSArray *saved=[NSUserDefaults.standardUserDefaults arrayForKey:@"SoundnetCollapsedNodeCategories"];
        _collapsedCategories=[NSMutableSet setWithArray:saved ?: @[]];
        _searchField=[[NSSearchField alloc]initWithFrame:NSMakeRect(8,37,MAX(80,frame.size.width-16),27)];
        _searchField.tag=990101;
        _searchField.font=[NSFont systemFontOfSize:11];
        _searchField.delegate=self;
        _searchField.target=self;_searchField.action=@selector(searchChanged:);
        _searchField.sendsSearchStringImmediately=YES;
        _searchField.sendsWholeSearchString=NO;
        [self addSubview:_searchField];
        _expandButton=[NSButton buttonWithTitle:@"" target:self action:@selector(expandAll:)];
        _collapseButton=[NSButton buttonWithTitle:@"" target:self action:@selector(collapseAll:)];
        for(NSButton *button in @[_expandButton,_collapseButton]) {
            button.bezelStyle=NSBezelStyleRoundRect;
            button.controlSize=NSControlSizeSmall;
            button.font=[NSFont systemFontOfSize:10.5];
            [self addSubview:button];
        }
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(languageChanged:)
            name:SNLanguageDidChangeNotification object:nil];
        [self rebuild];
    }
    return self;
}
-(void)dealloc {[NSNotificationCenter.defaultCenter removeObserver:self];}
-(BOOL)isFlipped{return YES;}
-(BOOL)isOpaque{return NO;}
static NSColor *categoryColor(NSString *category) {
    if([category isEqualToString:@"输入"])return [NSColor colorWithCalibratedRed:.40 green:.72 blue:.78 alpha:1];
    if([category isEqualToString:@"处理"])return [NSColor colorWithCalibratedRed:.58 green:.52 blue:.86 alpha:1];
    if([category isEqualToString:@"逻辑"])return [NSColor colorWithCalibratedRed:.90 green:.45 blue:.40 alpha:1];
    if([category isEqualToString:@"输出"])return [NSColor colorWithCalibratedRed:.45 green:.72 blue:.52 alpha:1];
    return [NSColor colorWithWhite:.55 alpha:1];
}
-(NSString *)query {
    return [_searchField.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
}
-(BOOL)matchesType:(int)type category:(NSString *)category query:(NSString *)query {
    if(!query.length)return YES;
    NSString *name=@(tri::nodeTitle(type));
    NSString *searchable=[NSString stringWithFormat:@"%@ %@ %@ %@ %s %d",name,SNEnglish(name),category,SNEnglish(category),tri::nodeKey(type),type];
    for(NSString *word in [query componentsSeparatedByCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet]) {
        if(word.length && [searchable rangeOfString:word options:(NSCaseInsensitiveSearch|NSDiacriticInsensitiveSearch)].location==NSNotFound)return NO;
    }
    return YES;
}
-(void)rebuild {
    _rows=[NSMutableArray new];_matchCount=0;_hover=-1;_pressed=-1;
    NSString *query=[self query];
    _searchField.placeholderString=SNLocalized(@"搜索节点…");
    _searchField.toolTip=SNLocalized(@"尝试名称、类别或节点标识");
    _expandButton.title=SNLocalized(@"展开全部");
    _collapseButton.title=SNLocalized(@"收起全部");
    _expandButton.enabled=!query.length;_collapseButton.enabled=!query.length;
    double y=108;
    // Iterate descriptors directly: search remains bilingual even when the
    // canvas/menu catalogue has already translated its display strings.
    for(NSString *category in @[@"输入",@"处理",@"逻辑",@"注释",@"输出"]) {
        NSMutableArray<NSNumber *> *items=[NSMutableArray new];
        for(int type:tri::nodeCatalog()) {
            if([@(tri::nodeCategory(type)) isEqualToString:category] && [self matchesType:type category:category query:query])
                [items addObject:@(type)];
        }
        if(!items.count)continue;
        _matchCount+=items.count;
        BOOL collapsed=!query.length && [_collapsedCategories containsObject:category];
        [_rows addObject:@{@"kind":@"header",@"title":SNLocalized(category),@"category":category,
            @"count":@(items.count),@"collapsed":@(collapsed),@"y":@(y),@"h":@28}];
        y+=30;
        if(!collapsed)for(NSNumber *type in items) {
            [_rows addObject:@{@"kind":@"item",@"title":SNLocalized(@(tri::nodeTitle(type.intValue))),
                @"type":type,@"category":category,@"y":@(y),@"h":@28}];
            y+=28;
        }
        y+=6;
    }
    _contentHeight=_rows.count?y+8:202;
    [self setFrameSize:NSMakeSize(self.bounds.size.width,self.enclosingScrollView.contentSize.height)];
    self.needsDisplay=YES;
}
-(void)languageChanged:(NSNotification *)notification {[self rebuild];}
-(void)setFrameSize:(NSSize)size {
    [super setFrameSize:NSMakeSize(size.width,MAX(_contentHeight,size.height))];
    _searchField.frame=NSMakeRect(8,37,MAX(80,size.width-16),27);
    CGFloat half=MAX(40,(size.width-22)/2);
    _expandButton.frame=NSMakeRect(8,72,half,23);
    _collapseButton.frame=NSMakeRect(14+half,72,half,23);
}
-(void)setSearchQuery:(NSString *)query {
    _searchField.stringValue=query ?: @"";
    [self searchChanged:nil];
}
-(void)searchChanged:(id)sender {
    [self rebuild];
    [self scrollPoint:NSZeroPoint];
}
-(void)controlTextDidChange:(NSNotification *)notification {
    if(notification.object==_searchField)[self searchChanged:nil];
}
-(void)saveCollapsedCategories {
    [NSUserDefaults.standardUserDefaults setObject:_collapsedCategories.allObjects forKey:@"SoundnetCollapsedNodeCategories"];
}
-(void)toggleCategory:(NSString *)category {
    if([self query].length || !category)return;
    if([_collapsedCategories containsObject:category])[_collapsedCategories removeObject:category];
    else [_collapsedCategories addObject:category];
    [self saveCollapsedCategories];[self rebuild];
}
-(void)expandAll:(id)sender {
    if([self query].length)return;
    [_collapsedCategories removeAllObjects];[self saveCollapsedCategories];[self rebuild];
}
-(void)collapseAll:(id)sender {
    if([self query].length)return;
    [_collapsedCategories addObjectsFromArray:@[@"输入",@"处理",@"逻辑",@"注释",@"输出"]];
    [self saveCollapsedCategories];[self rebuild];
}
-(NSInteger)rowAtPoint:(NSPoint)p {
    if(p.x<4||p.x>NSWidth(self.bounds)-4)return -1;
    for(NSUInteger i=0;i<_rows.count;++i) {
        NSDictionary *row=_rows[i];
        double y=[row[@"y"] doubleValue],h=[row[@"h"] doubleValue];
        if(p.y>=y && p.y<y+h)return (NSInteger)i;
    }
    return -1;
}
-(void)drawLibraryText:(NSString *)text rect:(NSRect)rect color:(NSColor *)color size:(CGFloat)size {
    NSMutableParagraphStyle *style=[NSMutableParagraphStyle new];style.lineBreakMode=NSLineBreakByTruncatingTail;
    [text drawInRect:rect withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:size],
        NSForegroundColorAttributeName:color,NSParagraphStyleAttributeName:style}];
}
-(void)drawRect:(NSRect)dirty {
    [[NSColor colorWithWhite:.115 alpha:1]setFill];
    [[NSBezierPath bezierPathWithRoundedRect:self.bounds xRadius:9 yRadius:9]fill];
    [self drawLibraryText:SNLocalized(@"节点库") rect:NSMakeRect(12,11,NSWidth(self.bounds)-55,18)
        color:[NSColor colorWithWhite:.9 alpha:1] size:12];
    [self drawLibraryText:[NSString stringWithFormat:@"%lu",(unsigned long)_matchCount]
        rect:NSMakeRect(NSWidth(self.bounds)-38,12,30,16) color:[NSColor colorWithWhite:.5 alpha:1] size:11];
    if(!_rows.count) {
        [self drawLibraryText:SNLocalized(@"没有匹配的节点") rect:NSMakeRect(12,122,NSWidth(self.bounds)-24,20)
            color:[NSColor colorWithWhite:.75 alpha:1] size:11.5];
        [self drawLibraryText:SNLocalized(@"尝试名称、类别或节点标识") rect:NSMakeRect(12,148,NSWidth(self.bounds)-24,35)
            color:[NSColor colorWithWhite:.48 alpha:1] size:10];
    }
    for(NSUInteger i=0;i<_rows.count;++i) {
        NSDictionary *row=_rows[i];
        double y=[row[@"y"] doubleValue],h=[row[@"h"] doubleValue];
        NSRect r=NSMakeRect(5,y,self.bounds.size.width-10,h);
        if(!NSIntersectsRect(dirty,r))continue;
        BOOL header=[row[@"kind"] isEqualToString:@"header"];
        BOOL hot=((NSInteger)i==_hover);
        if(hot || header) {
            [[NSColor colorWithWhite:1 alpha:hot?.075:.035]setFill];
            [[NSBezierPath bezierPathWithRoundedRect:r xRadius:5 yRadius:5]fill];
        }
        if(header) {
            [self drawLibraryText:[row[@"collapsed"] boolValue]?@"▸":@"▾" rect:NSMakeRect(11,y+5,12,18)
                color:[NSColor colorWithWhite:.6 alpha:1] size:12];
            [self drawLibraryText:row[@"title"] rect:NSMakeRect(27,y+6,r.size.width-65,17)
                color:categoryColor(row[@"category"]) size:11];
            [self drawLibraryText:[row[@"count"] stringValue] rect:NSMakeRect(NSWidth(self.bounds)-33,y+6,24,17)
                color:[NSColor colorWithWhite:.5 alpha:1] size:10.5];
            continue;
        }
        [categoryColor(row[@"category"]) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(14,y+h/2-2.5,5,5)]fill];
        [self drawLibraryText:row[@"title"] rect:NSMakeRect(28,y+6,r.size.width-32,18)
            color:[NSColor colorWithWhite:hot?.98:.81 alpha:1] size:11.5];
    }
}
-(void)updateTrackingAreas {
    [super updateTrackingAreas];
    for(NSTrackingArea *area in self.trackingAreas)[self removeTrackingArea:area];
    [self addTrackingArea:[[NSTrackingArea alloc]initWithRect:self.bounds
        options:(NSTrackingMouseMoved|NSTrackingMouseEnteredAndExited|NSTrackingActiveInKeyWindow|NSTrackingInVisibleRect)
        owner:self userInfo:nil]];
}
-(void)mouseMoved:(NSEvent *)event {
    NSInteger row=[self rowAtPoint:[self convertPoint:event.locationInWindow fromView:nil]];
    if(row==_hover)return;
    _hover=row;self.needsDisplay=YES;
    self.toolTip=row>=0 && [_rows[row][@"kind"] isEqualToString:@"item"]?_rows[row][@"title"]:nil;
}
-(void)mouseExited:(NSEvent *)event {_hover=-1;self.toolTip=nil;self.needsDisplay=YES;}
-(NSImage *)dragImageForTitle:(NSString *)title {
    NSImage *image=[[NSImage alloc]initWithSize:NSMakeSize(188,30)];
    [image lockFocus];
    [[NSColor colorWithWhite:.20 alpha:.96]setFill];
    [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(0,0,188,30) xRadius:6 yRadius:6]fill];
    [self drawLibraryText:title rect:NSMakeRect(10,7,168,17) color:NSColor.whiteColor size:11.5];
    [image unlockFocus];
    return image;
}
-(void)mouseDown:(NSEvent *)event {
    NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];
    _pressed=[self rowAtPoint:p];_pressPoint=p;_dragged=NO;
}
-(void)mouseDragged:(NSEvent *)event {
    if(_pressed<0||_pressed>=(NSInteger)_rows.count||_dragged)return;
    NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];
    if(std::hypot(p.x-_pressPoint.x,p.y-_pressPoint.y)<4)return;
    _dragged=YES;
    NSDictionary *row=_rows[_pressed];
    if(![row[@"kind"] isEqualToString:@"item"])return;
    NSPasteboardItem *item=[NSPasteboardItem new];
    [item setString:[row[@"type"] stringValue] forType:SNNodeTypePasteboard];
    NSDraggingItem *dragging=[[NSDraggingItem alloc]initWithPasteboardWriter:item];
    NSImage *image=[self dragImageForTitle:row[@"title"]];
    [dragging setDraggingFrame:NSMakeRect(_pressPoint.x-14,_pressPoint.y-12,image.size.width,image.size.height) contents:image];
    [self beginDraggingSessionWithItems:@[dragging] event:event source:self];
}
-(void)mouseUp:(NSEvent *)event {
    NSInteger released=[self rowAtPoint:[self convertPoint:event.locationInWindow fromView:nil]];
    if(_pressed<0||_pressed>=(NSInteger)_rows.count||_dragged||released!=_pressed){_pressed=-1;return;}
    NSDictionary *row=_rows[_pressed];_pressed=-1;
    if([row[@"kind"] isEqualToString:@"header"])[self toggleCategory:row[@"category"]];
    else if(_editor)[_editor insertFromLibrary:[row[@"type"] intValue]];
}
-(NSDragOperation)draggingSession:(NSDraggingSession *)session sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
    return NSDragOperationCopy;
}
@end

NSView *TRICreateEditor(TRIRead read,TRIWrite write,TRIMIDI midi,TRIInputRead inputRead,TRIInputWrite inputWrite,TRIStatus status,TRIAssetsRead assetsRead,TRIAssetsWrite assetsWrite,TRIProcessingRead processingRead,TRIProcessingWrite processingWrite,TRINodeStop nodeStop) {
    SNInstanceEditor *editor=[[SNInstanceEditor alloc]initWithFrame:NSMakeRect(0,0,1360,840)];
    editor.read=read;editor.write=write;editor.midi=midi;editor.inputRead=inputRead;editor.inputWrite=inputWrite;
    editor.nodeStop=nodeStop;
    editor.assetsRead=assetsRead;editor.assetsWrite=assetsWrite;
    editor.processingRead=processingRead;editor.processingWrite=processingWrite;editor.processingActive=processingRead?processingRead():YES;
    editor.libraryVisible=YES;editor.collapsedParameterGroups=[NSMutableSet new];
    editor.history=[NSMutableArray new];editor.future=[NSMutableArray new];editor.zoom=1;editor.origin=NSMakePoint(20,20);
    editor.selection=[NSMutableSet new];editor.selectedEdge=-1;editor.snap=NO;editor.keyboardVisible=YES;
    editor.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    editor.appearance=[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    editor.fileButtons=@[
        [editor button:SNLocalized(@"打开") frame:NSZeroRect action:@selector(open:) parent:editor],
        [editor button:SNLocalized(@"保存") frame:NSZeroRect action:@selector(save:) parent:editor],
        [editor button:SNLocalized(@"导出") frame:NSZeroRect action:@selector(exportWav:) parent:editor]];
    editor.editButtons=@[
        [editor button:SNLocalized(@"撤销") frame:NSZeroRect action:@selector(undo:) parent:editor],
        [editor button:SNLocalized(@"重做") frame:NSZeroRect action:@selector(redo:) parent:editor],
        [editor button:SNLocalized(@"复制") frame:NSZeroRect action:@selector(copy:) parent:editor],
        [editor button:SNLocalized(@"粘贴") frame:NSZeroRect action:@selector(paste:) parent:editor],
        [editor button:SNLocalized(@"查找") frame:NSZeroRect action:@selector(search:) parent:editor],
        [editor button:SNLocalized(@"吸附") frame:NSZeroRect action:@selector(snap:) parent:editor],
        [editor button:SNLocalized(@"排列") frame:NSZeroRect action:@selector(autoLayout:) parent:editor],
        [editor button:SNLocalized(@"适配") frame:NSZeroRect action:@selector(fit:) parent:editor],
        [editor button:SNLocalized(@"清空") frame:NSZeroRect action:@selector(blank:) parent:editor]];
    [editor.editButtons[5] setButtonType:NSButtonTypePushOnPushOff];
    editor.playButton=[editor button:SNLocalized(@"播放") frame:NSZeroRect action:@selector(playGraph:) parent:editor];
    editor.stopButton=[editor button:SNLocalized(@"停止") frame:NSZeroRect action:@selector(stopGraph:) parent:editor];
    editor.stopButton.keyEquivalent=@".";editor.stopButton.keyEquivalentModifierMask=NSEventModifierFlagCommand;
    editor.playButton.image=[NSImage imageWithSystemSymbolName:@"play.fill" accessibilityDescription:nil];
    editor.stopButton.image=[NSImage imageWithSystemSymbolName:@"stop.fill" accessibilityDescription:nil];
    editor.playButton.imagePosition=NSImageLeft;editor.stopButton.imagePosition=NSImageLeft;
    editor.playButton.contentTintColor=NSColor.systemGreenColor;
    editor.processingLabel=[NSTextField labelWithString:@""];editor.processingLabel.font=[NSFont systemFontOfSize:11 weight:NSFontWeightMedium];
    [editor addSubview:editor.processingLabel];
    editor.libraryToggle=[editor button:SNLocalized(@"节点库") frame:NSZeroRect action:@selector(toggleLibrary:) parent:editor];
    editor.libraryToggle.image=[NSImage imageWithSystemSymbolName:@"sidebar.left" accessibilityDescription:nil];
    editor.libraryToggle.imagePosition=NSImageLeft;
    [editor.libraryToggle setButtonType:NSButtonTypePushOnPushOff];editor.libraryToggle.state=NSControlStateValueOn;
    editor.languagePopup=[[NSPopUpButton alloc]initWithFrame:NSZeroRect pullsDown:NO];
    [editor.languagePopup addItemsWithTitles:@[@"中文",@"English"]];
    editor.languagePopup.target=editor;editor.languagePopup.action=@selector(languageChanged:);
    editor.languagePopup.accessibilityLabel=@"Language / 语言";[editor addSubview:editor.languagePopup];
    NSTextField *heading=[NSTextField labelWithString:@"Soundnet 2.90"];
    heading.font=[NSFont systemFontOfSize:18 weight:NSFontWeightSemibold];
    editor.heading=heading;editor.headingText=heading.stringValue;[editor addSubview:heading];
    editor.cancelButton=[editor button:SNLocalized(@"取消处理") frame:NSZeroRect action:@selector(cancelProcessing:) parent:editor];
    editor.cancelButton.hidden=YES;
    editor.exportProgress=[[NSProgressIndicator alloc]initWithFrame:NSZeroRect];
    editor.exportProgress.style=NSProgressIndicatorStyleSpinning;editor.exportProgress.controlSize=NSControlSizeSmall;
    editor.exportProgress.hidden=YES;[editor addSubview:editor.exportProgress];
    [[NSNotificationCenter defaultCenter] addObserver:editor selector:@selector(refreshLanguage:) name:SNLanguageDidChangeNotification object:nil];

    NSScrollView *libraryScroll=[[NSScrollView alloc]initWithFrame:NSMakeRect(8,48,196,600)];
    libraryScroll.hasVerticalScroller=YES;libraryScroll.drawsBackground=NO;
    libraryScroll.borderType=NSNoBorder;libraryScroll.autohidesScrollers=NO;
    SNNodeLibrary *library=[[SNNodeLibrary alloc]initWithFrame:NSMakeRect(0,0,194,600)];
    library.editor=editor;editor.library=library;editor.libraryScroll=libraryScroll;
    libraryScroll.documentView=library;[library rebuild];[editor addSubview:libraryScroll];

    SNInstanceCanvas *canvas=[[SNInstanceCanvas alloc]initWithFrame:NSMakeRect(214,48,700,600)];
    canvas.editor=editor;editor.canvas=canvas;[editor addSubview:canvas];

    NSScrollView *scroll=[[NSScrollView alloc]initWithFrame:NSMakeRect(1010,48,262,600)];
    scroll.hasVerticalScroller=YES;editor.inspectorScroll=scroll;
    editor.inspector=[[SNFlippedView alloc]initWithFrame:NSMakeRect(0,0,260,520)];
    scroll.documentView=editor.inspector;[editor addSubview:scroll];

    editor.status=[NSTextField labelWithString:SNLocalized(@"左侧节点库可点击或拖入画布 · 拖动框选 · Option 拖动平移 · ⌘滚轮缩放 · ⇧⌘G 编组")];
    editor.status.font=[NSFont systemFontOfSize:11];
    editor.status.textColor=[NSColor secondaryLabelColor];
    editor.status.frame=NSMakeRect(12,640,1100,22);[editor addSubview:editor.status];

    NSButton *keyboard=[NSButton checkboxWithTitle:SNLocalized(@"钢琴键盘") target:editor action:@selector(toggleKeyboard:)];
    [keyboard setButtonType:NSButtonTypePushOnPushOff];
    keyboard.state=NSControlStateValueOn;keyboard.font=[NSFont systemFontOfSize:11];
    keyboard.frame=NSMakeRect(1160,640,104,24);
    editor.keyboardToggle=keyboard;[editor addSubview:keyboard];

    NSButton *zoomOut=[NSButton buttonWithTitle:@"−" target:editor action:@selector(zoomOut:)];
    zoomOut.frame=NSMakeRect(224,620,26,22);zoomOut.bezelStyle=NSBezelStyleRounded;
    NSButton *zoomIn=[NSButton buttonWithTitle:@"+" target:editor action:@selector(zoomIn:)];
    zoomIn.frame=NSMakeRect(286,620,26,22);zoomIn.bezelStyle=NSBezelStyleRounded;
    NSTextField *zoomLabel=[NSTextField labelWithString:@"100%"];
    zoomLabel.frame=NSMakeRect(252,621,34,18);zoomLabel.alignment=NSTextAlignmentCenter;
    zoomLabel.font=[NSFont monospacedDigitSystemFontOfSize:10 weight:NSFontWeightMedium];
    zoomLabel.textColor=[NSColor secondaryLabelColor];
    NSButton *zoomFit=[NSButton buttonWithTitle:SNLocalized(@"适配") target:editor action:@selector(fit:)];
    zoomFit.frame=NSMakeRect(316,620,44,22);zoomFit.bezelStyle=NSBezelStyleRounded;
    zoomFit.font=[NSFont systemFontOfSize:10];
    editor.zoomLabel=zoomLabel;
    editor.zoomControls=@[zoomOut,zoomLabel,zoomIn,zoomFit];
    for(NSView *control in editor.zoomControls)[editor addSubview:control];

    SNPiano *piano=[[SNPiano alloc]initWithFrame:NSMakeRect(8,690,1264,84)];piano.send=midi;
    editor.piano=piano;[editor addSubview:piano];

    NSDictionary *assets=assetsRead?assetsRead():nil;
    if(assets[@"graph"])editor.document=copyJSON(assets[@"graph"]);
    else {
        NSDictionary *legacy=ternaryfile::snapshot(read,assetsRead);
        editor.document=copyJSON([editor migrate:legacy] ?: @{@"version":@4,@"projectId":@"local",@"nodes":@[],@"edges":@[]});
        if(assets)[editor commit:nil];
    }
    [editor refreshLanguage:nil];
    [editor layoutUI];
    [editor updateZoomLabel];
    [editor fit:nil];[editor inspect];
    __weak SNInstanceEditor *weak=editor;
    editor.timer=[NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer*){[weak tick];}];
    return editor;
}
