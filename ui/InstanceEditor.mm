#import "TriEditor.h"
#import "WorkflowFile.hpp"
#import "../src/WorkflowAssets.h"
#import "../src/OfflineRender.hpp"
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
        if(meta)[rows addObject:@[@(type),@(meta->title),@(meta->category)]];
    }
    return rows;
}
NSString *title(int type) { return @(tri::nodeTitle(type)); }
static NSString *nodeLabel(NSDictionary *node) {
    NSString *name=node[@"name"];
    if([name isKindOfClass:NSString.class] && name.length)return name;
    return title([node[@"type"] intValue]);
}
static NSArray<NSString*> *paramOptions(int p) {
    if(p==tri::Wave1 || p==tri::Wave2 || p==tri::Wave3)
        return @[@"正弦",@"方波",@"锯齿",@"三角"];
    if(p==tri::Pattern)
        return @[@"直八",@"Bossa Nova",@"Reggae",@"Disco",@"十六分脉冲"];
    if(p==tri::ChordQuality)
        return @[@"大三和弦",@"小三和弦",@"属七",@"大七",@"小七",@"减三",@"挂二",@"挂四"];
    if(p==tri::FilterType)
        return @[@"低通",@"高通",@"带通"];
    if(p==tri::NoiseType)
        return @[@"白噪声",@"粉红噪声"];
    if(p==tri::QuantizeScale)
        return @[@"大调",@"小调",@"多利亚",@"五声",@"半音"];
    if(p==tri::QuantizeMode)
        return @[@"就近",@"向上",@"向下"];
    if(p==tri::QuantizeRoot)
        return @[@"C",@"C#",@"D",@"D#",@"E",@"F",@"F#",@"G",@"G#",@"A",@"A#",@"B"];
    if(p==tri::ArpOn || p==tri::ClipRun || p==tri::ClipLoop || p==tri::ProgressRun ||
       p==tri::ChordRun || p==tri::SamplerLoop || p==tri::AudioClipLoop ||
       p==tri::SamplerReverse || p==tri::AudioClipReverse)
        return @[@"关闭",@"开启"];
    if(p==tri::ClockSync)
        return @[@"自由",@"同步宿主"];
    return nil;
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
std::vector<int> controls(int type) { return tri::nodeParamList(type); }
NSString *parameterName(int p) {
    switch(p) {
        case tri::TriggerCount:return @"收到 X 次后触发";
        case tri::WaitSeconds:return @"等待秒数";
        case tri::ClockBPM:return @"BPM";
        case tri::ClipRun:return @"播放  0 / 1";
        case tri::ClipLoop:return @"循环  0 / 1";
        default:return @(tri::graphParamInfo(p).name);
    }
}
}

namespace { NSRect nodeRect(NSDictionary *node); }

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
- (void)keyDown:(NSEvent *)event {
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
-(void)refreshClipFields;
-(void)autosave;
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
-(void)commit:(NSDictionary *)previous;
-(void)insert:(NSMenuItem *)sender;
-(BOOL)dropFileURLs:(NSArray<NSURL *> *)urls atPoint:(NSPoint)point clipMode:(BOOL)clipMode;
-(NSPoint)world:(NSPoint)point;
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
            {"大厅",{{tri::ReverbMix,.45f},{tri::ReverbSize,.9f},{tri::ReverbDamping,.3f},{tri::ReverbWidth,1}}} };
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
            {"宽阔",{{tri::ChorusRate,2.5f},{tri::ChorusDepth,.6f},{tri::ChorusMix,.45f},{tri::ChorusDelay,20}}} };
        case tri::TremoloNode: return {
            {"慢速",{{tri::TremoloRate,2},{tri::TremoloDepth,.4f},{tri::TremoloMix,.8f},{tri::TremoloStereo,0}}},
            {"快速",{{tri::TremoloRate,8},{tri::TremoloDepth,.6f},{tri::TremoloMix,1},{tri::TremoloStereo,1}}} };
        case tri::BitcrusherNode: return {
            {"8-bit",{{tri::CrushBits,8},{tri::CrushRate,12000},{tri::CrushMix,.8f}}},
            {"破碎",{{tri::CrushBits,4},{tri::CrushRate,4000},{tri::CrushMix,1}}} };
        case tri::PhaserNode: return {
            {"慢速扫频",{{tri::PhaserRate,.3f},{tri::PhaserDepth,.8f},{tri::PhaserFeedback,.35f},{tri::PhaserMix,.5f}}},
            {"喷气",{{tri::PhaserRate,1.5f},{tri::PhaserDepth,.6f},{tri::PhaserFeedback,.6f},{tri::PhaserMix,.6f}}} };
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
-(void)drawRect:(NSRect)dirty {[NSColor.windowBackgroundColor setFill];NSRectFill(self.bounds);}
-(BOOL)isFlipped{return YES;}
-(void)dealloc {[_timer invalidate];}
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
-(void)message:(NSString *)text {_status.stringValue=text ?: @"";}
-(void)commit:(NSDictionary *)previous {
    tri::GraphSpec graph;NSString *why=nil;
    if(!triassets::decodeGraph(_document,graph,&why) || !_assetsWrite || !_assetsWrite(@{@"graph":_document})) {
        _document=copyJSON(previous) ?: [@{@"version":@3,@"nodes":[NSMutableArray new],@"edges":[NSMutableArray new]}mutableCopy];[self message:why ?: @"宿主无法应用节点图。"];
    } else {
        if(previous && ![previous isEqual:_document]) {
            [_history addObject:copyJSON(previous)];[_future removeAllObjects];
            while(_history.count>64)[_history removeObjectAtIndex:0];
        }
        [self autosave];
        [self message:[NSString stringWithFormat:@"%lu 个独立节点 · %lu 条连接",(unsigned long)[_document[@"nodes"] count],(unsigned long)[_document[@"edges"] count]]];
    }
    [self pruneSelection];
    _canvas.needsDisplay=YES;
}
-(void)autosave {
    if(getenv("SOUNDNET_NO_AUTOSAVE"))return;
    NSString *path=autosaveFilePath();if(!path.length)return;
    NSData *data=[NSJSONSerialization dataWithJSONObject:_document options:0 error:nil];
    if(data)[data writeToFile:path atomically:YES];
}
-(void)restoreAutosave:(id)sender {
    NSString *path=autosaveFilePath();
    NSData *data=path.length?[NSData dataWithContentsOfFile:path]:nil;
    if(!data){[self message:@"没有可恢复的自动保存"];return;}
    id object=[NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:nil];
    tri::GraphSpec graph;NSString *why=nil;
    if(![object isKindOfClass:NSDictionary.class] || !triassets::decodeGraph(object,graph,&why)) {
        [self message:why ?: @"自动保存文件无效"];return;
    }
    NSDictionary *previous=copyJSON(_document);
    _document=copyJSON(object);
    [_selection removeAllObjects];_selected=0;_selectedEdge=-1;
    [self commit:previous];[self inspect];[self fit:nil];
    [self message:@"已恢复自动保存"];
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
    NSButton *b=[NSButton buttonWithTitle:text target:self action:action];b.frame=frame;[parent addSubview:b];return b;
}
-(NSMutableDictionary *)makeNode:(int)type at:(NSPoint)point id:(uint64_t)uid {
    NSMutableDictionary *parameters=[NSMutableDictionary new];
    for(int p:controls(type))parameters[[@(p) stringValue]]=@(tri::graphParamInfo(p).def);
    if(type==tri::Osc1Node)parameters[[@(tri::Level1) stringValue]]=@.66;
    NSMutableDictionary *node=[@{@"id":@(uid),@"type":@(type),@"x":@(point.x),@"y":@(point.y),@"parameters":parameters} mutableCopy];
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
    if(!urls.count) return NO;
    NSDictionary *previous=copyJSON(_document);
    uint64_t next=1;
    for(NSDictionary *node in _document[@"nodes"])next=std::max(next,[node[@"id"] unsignedLongLongValue]+1);
    BOOL added=NO; uint64_t last=0; NSUInteger offset=0; NSMutableArray<NSString *> *errors=[NSMutableArray new];
    for(NSURL *url in urls) {
        NSString *extension=url.pathExtension.lowercaseString;
        NSString *kind=nil; int type=0;
        if([extension isEqualToString:@"mid"]||[extension isEqualToString:@"midi"]) {kind=@"clip";type=tri::ClipNode;}
        else if([extension isEqualToString:@"wav"]||[extension isEqualToString:@"aif"]||[extension isEqualToString:@"aiff"]||[extension isEqualToString:@"aifc"]) {
            kind=@"audio";type=clipMode?tri::AudioClipNode:tri::SamplerNode;
        } else if([extension isEqualToString:@"mlmodel"]||[extension isEqualToString:@"mlpackage"]||[extension isEqualToString:@"mlmodelc"]) {
            kind=@"model";type=tri::ModelNode;
        } else {
            [errors addObject:[NSString stringWithFormat:@"%@：不支持的格式",url.lastPathComponent]];
            continue;
        }
        NSString *why=nil;
        NSDictionary *asset=triassets::importAsset(kind,url,&why);
        if(!asset) {
            [errors addObject:[NSString stringWithFormat:@"%@：%@",url.lastPathComponent,why?:@"无法读取"]];
            continue;
        }
        const uint64_t uid=next++;
        NSMutableDictionary *node=[self makeNode:type at:NSMakePoint(point.x+offset*30,point.y+offset*30) id:uid];
        node[@"asset"]=asset;
        if(type==tri::ClipNode) {
            NSMutableDictionary *params=node[@"parameters"];
            params[[@(tri::ClipRun) stringValue]]=@1;
            params[[@(tri::ClipLoop) stringValue]]=@1;
        } else if(type==tri::AudioClipNode) {
            node[@"parameters"][[@(tri::AudioClipLoop) stringValue]]=@1;
        }
        last=uid; added=YES; ++offset;
    }
    if(!added) {
        if(errors.count) [self message:errors.firstObject];
        NSBeep();
        return NO;
    }
    _selected=last;
    [_selection removeAllObjects];[_selection addObject:@(last)];
    [self commit:previous];[self inspect];
    if(errors.count) [self message:[NSString stringWithFormat:@"已添加 %lu 个节点；%@",(unsigned long)offset,errors.firstObject]];
    else [self message:[NSString stringWithFormat:@"已拖入 %lu 个文件",(unsigned long)offset]];
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
    NSArray<NSNumber *> *ids=[self activeIDs];if(!ids.count){[self message:@"没有可复制的节点。"];return;}
    NSSet *wanted=[NSSet setWithArray:ids];
    NSMutableArray *nodes=[NSMutableArray new],*edges=[NSMutableArray new];
    for(NSNumber *key in ids) { NSDictionary *node=[self node:key.unsignedLongLongValue]; if(node)[nodes addObject:copyJSON(node)]; }
    for(NSDictionary *edge in _document[@"edges"])
        if([wanted containsObject:edge[@"source"]] && [wanted containsObject:edge[@"target"]])[edges addObject:copyJSON(edge)];
    _clipboard=@{@"nodes":nodes,@"edges":edges};
    [self message:[NSString stringWithFormat:@"已复制 %lu 个节点。",(unsigned long)nodes.count]];
}
-(void)cut:(id)sender {
    NSArray<NSNumber *> *ids=[self activeIDs];if(!ids.count)return;
    [self copy:nil];[self remove:nil];[self message:[NSString stringWithFormat:@"已剪切 %lu 个节点。",(unsigned long)ids.count]];
}
-(void)paste:(id)sender {
    NSDictionary *source=_clipboard;
    if(!source){[self message:@"剪贴板为空。"];return;}
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
    [self message:[NSString stringWithFormat:@"已粘贴 %lu 个节点。",(unsigned long)copies.count]];
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
    [self message:_snap?@"吸附已开启（20 点网格）。":@"吸附已关闭。"];
}
-(void)search:(id)sender {
    NSAlert *alert=[NSAlert new];alert.messageText=@"查找节点";alert.informativeText=@"输入节点名称或编号。";
    [alert addButtonWithTitle:@"查找"];[alert addButtonWithTitle:@"取消"];
    NSTextField *field=[[NSTextField alloc]initWithFrame:NSMakeRect(0,0,240,24)];
    field.placeholderString=@"例如 滤波器、混响、27";alert.accessoryView=field;
    if([alert runModal]!=NSAlertFirstButtonReturn)return;
    NSString *needle=[field.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet].lowercaseString;
    if(!needle.length)return;
    NSDictionary *found=nil;
    for(NSDictionary *node in _document[@"nodes"]) {
        int type=[node[@"type"] intValue];
        NSString *labelText=[[NSString stringWithFormat:@"%@ %@",nodeLabel(node),node[@"id"]] lowercaseString];
        if([[node[@"id"] stringValue] isEqualToString:needle] || [labelText containsString:needle]){found=node;break;}
    }
    if(!found){[self message:@"没有找到匹配的节点。"];return;}
    [self selectOnly:[found[@"id"] unsignedLongLongValue]];
    NSRect r=nodeRect(found);
    _origin=NSMakePoint(_canvas.bounds.size.width/2-(NSMidX(r))*_zoom,_canvas.bounds.size.height/2-(NSMidY(r))*_zoom);
    _canvas.needsDisplay=YES;
    [self message:[NSString stringWithFormat:@"已定位 %@ #%@",title([found[@"type"] intValue]),found[@"id"]]];
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
        value<info.min || value>info.max) { [self message:@"参数超出范围。"];return; }
    NSDictionary *previous=copyJSON(_document);
    node[@"parameters"][[@(p) stringValue]]=@(value);
    NSSlider *slider=[_inspector viewWithTag:p+100000];
    if([slider isKindOfClass:[NSSlider class]])slider.doubleValue=value;
    [self commit:previous];[self.window makeFirstResponder:_canvas];
}
-(void)sliderChanged:(NSSlider *)slider {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    int p=(int)slider.tag-100000;const auto& info=tri::graphParamInfo(p);
    double value=slider.doubleValue;
    if(info.max-info.min<=12)value=std::round(value);
    if(value<info.min||value>info.max)return;
    NSTextField *field=[_inspector viewWithTag:p];
    if([field isKindOfClass:[NSTextField class]])field.stringValue=[NSString stringWithFormat:@"%.3g",value];
    NSDictionary *previous=copyJSON(_document);
    node[@"parameters"][[@(p) stringValue]]=@(value);
    [self commit:previous];
}
-(void)trigger:(id)sender {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    NSDictionary *previous=copyJSON(_document);
    NSString *key=[@(tri::TriggerPulse) stringValue];
    node[@"parameters"][key]=@(([node[@"parameters"][key] intValue]+1)%1000000);
    [self commit:previous];
}
-(void)bypassChanged:(NSButton *)sender {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    NSDictionary *previous=copyJSON(_document);
    if(sender.state==NSControlStateValueOn)node[@"bypass"]=@YES;
    else [node removeObjectForKey:@"bypass"];
    [self commit:previous];[self inspect];
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
    NSPanel *panel=[[NSPanel alloc]initWithContentRect:NSMakeRect(0,0,840,568)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable
        backing:NSBackingStoreBuffered defer:NO];
    panel.title=[NSString stringWithFormat:@"编辑 MIDI Clip · %@",asset[@"name"] ?: @"新片段"];
    SNNoteRollView *roll=[[SNNoteRollView alloc]initWithFrame:NSMakeRect(0,64,840,504)];
    roll.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    roll.notes=notes;roll.lengthBeats=length;roll.selectedIndex=notes.count?0:-1;
    [panel.contentView addSubview:roll];
    NSTextField *clipLabel=[NSTextField labelWithString:@"总长度（拍）"];
    clipLabel.frame=NSMakeRect(12,18,92,22);[panel.contentView addSubview:clipLabel];
    NSTextField *clipField=[[NSTextField alloc]initWithFrame:NSMakeRect(104,16,64,24)];
    clipField.stringValue=[NSString stringWithFormat:@"%.2f",length];
    clipField.target=self;clipField.action=@selector(clipLengthChanged:);
    [panel.contentView addSubview:clipField];
    NSTextField *noteLabel=[NSTextField labelWithString:@"音符长度"];
    noteLabel.frame=NSMakeRect(184,18,64,22);[panel.contentView addSubview:noteLabel];
    NSTextField *noteField=[[NSTextField alloc]initWithFrame:NSMakeRect(248,16,58,24)];
    noteField.target=self;noteField.action=@selector(clipNoteLengthChanged:);
    [panel.contentView addSubview:noteField];
    NSTextField *velocityLabel=[NSTextField labelWithString:@"力度"];
    velocityLabel.frame=NSMakeRect(318,18,34,22);[panel.contentView addSubview:velocityLabel];
    NSTextField *velocityField=[[NSTextField alloc]initWithFrame:NSMakeRect(352,16,48,24)];
    velocityField.target=self;velocityField.action=@selector(clipVelocityChanged:);
    [panel.contentView addSubview:velocityField];
    NSButton *deleteButton=[NSButton buttonWithTitle:@"删除音符" target:self action:@selector(clipDelete:)];
    deleteButton.frame=NSMakeRect(406,14,80,30);[panel.contentView addSubview:deleteButton];
    NSTextField *gridLabel=[NSTextField labelWithString:@"网格"];
    gridLabel.frame=NSMakeRect(494,18,30,22);[panel.contentView addSubview:gridLabel];
    NSPopUpButton *gridPopup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(526,14,58,26) pullsDown:NO];
    for(NSString *title in @[@"1/4",@"1/8",@"1/16",@"1/32"])[gridPopup addItemWithTitle:title];
    [gridPopup selectItemAtIndex:2];gridPopup.tag=3;gridPopup.target=self;gridPopup.action=@selector(clipGridChanged:);
    [panel.contentView addSubview:gridPopup];
    NSButton *quantize=[NSButton buttonWithTitle:@"量化" target:self action:@selector(clipQuantize:)];
    quantize.frame=NSMakeRect(590,14,60,30);[panel.contentView addSubview:quantize];
    NSButton *cancel=[NSButton buttonWithTitle:@"取消" target:self action:@selector(clipCancel:)];
    cancel.frame=NSMakeRect(658,14,66,30);[panel.contentView addSubview:cancel];
    NSButton *apply=[NSButton buttonWithTitle:@"应用" target:self action:@selector(clipApply:)];
    apply.frame=NSMakeRect(730,14,92,30);apply.keyEquivalent=@"\r";[panel.contentView addSubview:apply];
    NSTextField *hint=[NSTextField labelWithString:@"单击空白添加 · 拖动移动 · 拖右边缘改长度 · 右键/Delete 删除 · 量化按网格对齐"];
    hint.frame=NSMakeRect(12,38,810,18);hint.font=[NSFont systemFontOfSize:10];
    hint.textColor=[NSColor secondaryLabelColor];[panel.contentView addSubview:hint];
    __weak SNInstanceEditor *weakSelf=self;
    roll.selectionChanged=^{ [weakSelf refreshClipFields]; };
    self.clipPanel=panel;self.clipRoll=roll;self.clipUID=_selected;
    self.clipLengthField=clipField;self.clipNoteLengthField=noteField;self.clipVelocityField=velocityField;
    [self refreshClipFields];
    [self.window beginSheet:panel completionHandler:^(NSModalResponse response){(void)response;}];
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
    NSMutableDictionary *node=[self node:_clipUID]; if(!node||!_clipRoll){[self message:@"节点已不存在"];return;}
    if(!_clipRoll.notes.count){[self message:@"请先添加至少一个音符"];return;}
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
    if(!asset){[self message:why ?: @"无法编码 MIDI Clip"];return;}
    node[@"asset"]=asset;
    [self commit:previous];
    [self inspect];
    [self.window endSheet:_clipPanel];
    self.clipPanel=nil;self.clipRoll=nil;self.clipLengthField=nil;self.clipNoteLengthField=nil;self.clipVelocityField=nil;
}
-(void)clipCancel:(id)sender {
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
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    uint64_t uid=_selected;
    int nodeType=[node[@"type"] intValue];
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
        [self message:@"正在载入素材…"];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
            NSString *kind=clip?@"clip":audio?@"audio":@"model";
            NSString *why=nil;NSDictionary *asset=triassets::importAsset(kind,panel.URL,&why);
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
    if(_selection.count>1) {
        [self label:[NSString stringWithFormat:@"已选择 %lu 个节点",(unsigned long)_selection.count] y:12];
        [self label:@"Shift 点击增减选择，Shift 拖动框选。拖动任一节点可整体移动。" y:40];
        [self button:@"复制节点" frame:NSMakeRect(12,78,105,28) action:@selector(duplicate:) parent:_inspector];
        [self button:@"删除节点" frame:NSMakeRect(132,78,105,28) action:@selector(remove:) parent:_inspector];
        [self button:@"断开连线" frame:NSMakeRect(12,112,105,28) action:@selector(disconnect:) parent:_inspector];
        [self button:@"取消选择" frame:NSMakeRect(132,112,105,28) action:@selector(selectNone:) parent:_inspector];
        return;
    }
    NSMutableDictionary *node=[self node:_selected];
    if(!node){[self label:@"右键画布插入节点。拖动输出端口接到输入端口。" y:12];return;}
    int type=[node[@"type"] intValue];
    [self label:[NSString stringWithFormat:@"%@  #%llu",nodeLabel(node),_selected] y:12];
    NSTextField *nameField=[[NSTextField alloc]initWithFrame:NSMakeRect(12,32,225,24)];
    nameField.stringValue=node[@"name"] ?: @"";nameField.placeholderString=@"自定义名称（可选）";
    nameField.tag=990001;nameField.target=self;nameField.action=@selector(nameChanged:);
    [nameField setAccessibilityLabel:@"节点名称"];
    [_inspector addSubview:nameField];
    [self button:@"复制节点" frame:NSMakeRect(12,58,105,28) action:@selector(duplicate:) parent:_inspector];
    [self button:@"删除节点" frame:NSMakeRect(132,58,105,28) action:@selector(remove:) parent:_inspector];
    double y=100;
    const tri::NodeMeta *meta=tri::nodeMeta(type);
    if(meta && meta->bypassable) {
        NSButton *bypass=[NSButton checkboxWithTitle:@"旁通此节点（保留连线）" target:self action:@selector(bypassChanged:)];
        bypass.frame=NSMakeRect(12,y,230,28);bypass.state=node[@"bypass"] ? NSControlStateValueOn : NSControlStateValueOff;
        [_inspector addSubview:bypass];y+=38;
    }
    auto presets=presetsFor(type);
    if(!presets.empty()) {
        [self label:@"预设" y:y];
        NSPopUpButton *popup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(70,y-4,172,26) pullsDown:NO];
        [popup addItemWithTitle:@"选择预设…"];
        for(const auto& preset:presets)[popup addItemWithTitle:[NSString stringWithUTF8String:preset.name]];
        popup.target=self;popup.action=@selector(applyPreset:);
        [_inspector addSubview:popup];y+=38;
    }
    if(!controls(type).empty() || type==tri::ArrangerNode) {
        [self button:@"恢复默认参数" frame:NSMakeRect(12,y,225,28) action:@selector(resetParams:) parent:_inspector];y+=38;
    }
    if(type==tri::ArrangerNode)y=[self arrangerInspector:node y:y];
    else for(int p:controls(type)) {
        [self label:parameterName(p) y:y];
        const auto& info=tri::graphParamInfo(p);
        id val=node[@"parameters"][[@(p) stringValue]] ?: @(info.def);
        NSArray<NSString*> *options=paramOptions(p);
        if(options) {
            NSPopUpButton *popup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(142,y,96,26) pullsDown:NO];
            [popup addItemsWithTitles:options];
            NSInteger index=(NSInteger)std::lround([val doubleValue]-info.min);
            [popup selectItemAtIndex:std::clamp<NSInteger>(index,0,(NSInteger)options.count-1)];
            popup.tag=p;popup.target=self;popup.action=@selector(choiceChanged:);
            [popup setAccessibilityLabel:parameterName(p)];
            [_inspector addSubview:popup];y+=48;
            continue;
        }
        NSTextField *field=[[NSTextField alloc]initWithFrame:NSMakeRect(142,y,96,25)];
        field.stringValue=[val description];field.tag=p;field.target=self;field.action=@selector(changed:);
        [_inspector addSubview:field];
        if(info.max-info.min<=20000) {
            NSSlider *slider=[NSSlider sliderWithValue:[val doubleValue] minValue:info.min maxValue:info.max target:self action:@selector(sliderChanged:)];
            slider.frame=NSMakeRect(12,y+27,225,18);slider.tag=p+100000;slider.continuous=NO;
            [_inspector addSubview:slider];
            y+=58;
        } else y+=48;
    }
    if(type==tri::StartNode || type==tri::StopNode){
        [self button:@"发送触发信号" frame:NSMakeRect(12,y,225,30) action:@selector(trigger:) parent:_inspector];y+=44;
    }
    if(type==tri::WaitNode){[self label:@"收到开始或停止信号后，等待指定秒数再转发。可排队等待多个信号。" y:y];y+=65;}
    if(type==tri::CountNode){[self label:@"每收到 X 个信号后输出一次，再开始下一轮计数。可连接 Clip 的循环完成端口。" y:y];y+=65;}
    if(type==tri::RandomNode){[self label:@"收到触发后按 Chance 概率决定是否继续转发，适合生成随机段落或概率门限。" y:y];y+=52;}
    if(type==tri::MetronomeNode){[self label:@"开始触发后按 BPM 产生节拍，停止触发后静音。Beats 控制每小节拍数，Accent 控制第一拍重音。" y:y];y+=65;}
    if(type==tri::TransposeNode){[self label:@"把输入音符按半音移调，范围外自动夹到 0–127。旁通时保持原音高。" y:y];y+=52;}
    if(type==tri::EchoNode){[self label:@"为输入音符安排多次延迟重复，每次可按 Pitch 移调并按 Feedback 衰减力度。原音立即通过。" y:y];y+=65;}
    if(type==tri::GateNode){[self label:@"低于 Threshold 的音频会被压低；Range 控制关闭时的衰减深度，Attack/Release 控制开合速度。" y:y];y+=65;}
    if(type==tri::ChannelFilterNode){[self label:@"只放行指定 MIDI 通道（1–16）；0 表示全部通道，旁通时不过滤。" y:y];y+=60;}
    if(type==tri::ClipNode || type==tri::ModelNode) {
        [self button:type==tri::ClipNode?@"载入 MIDI Clip…":@"载入音符模型…" frame:NSMakeRect(12,y,225,30) action:@selector(import:) parent:_inspector];y+=40;
        if(type==tri::ClipNode) {
            [self button:@"编辑音符…" frame:NSMakeRect(12,y,225,30) action:@selector(editClip:) parent:_inspector];y+=40;
        }
        [self label:node[@"asset"][@"name"] ?: @"未载入素材" y:y];y+=46;
    }
    if(type==tri::SamplerNode || type==tri::AudioClipNode) {
        [self button:@"载入音频（WAV / AIFF / MP3 / M4A / AAC / CAF / FLAC）…" frame:NSMakeRect(12,y,225,30) action:@selector(import:) parent:_inspector];y+=42;
        NSDictionary *asset=node[@"asset"];
        if(asset[@"name"]) {
            [self label:asset[@"name"] y:y];y+=24;
            double rate=[asset[@"rate"] doubleValue];int channels=[asset[@"channels"] intValue];
            NSData *pcm=[[NSData alloc]initWithBase64EncodedString:asset[@"pcm"] ?: @"" options:0];
            double seconds=rate>0 ? double(pcm.length)/4.0/double(std::max(1,channels))/rate : 0;
            [self label:[NSString stringWithFormat:@"%.2f 秒 · %@ · %.0f Hz",seconds,channels==1?@"单声道":@"立体声",rate] y:y];y+=26;
            SNWaveformView *wave=[[SNWaveformView alloc]initWithFrame:NSMakeRect(12,y,225,58)];
            wave.pcm=pcm;wave.channels=channels;[_inspector addSubview:wave];y+=66;
        } else [self label:@"未载入音频素材" y:y],y+=28;
        if(type==tri::SamplerNode)
            [self label:@"音符触发复音采样。Root 为原始音高，Start/End 为采样区间，Loop 循环，Attack/Release 消除爆音；Reverse 反向播放，Velocity 为力度敏感度。" y:y],y+=74;
        else
            [self label:@"音频片段由开始/停止触发器控制。Loop 循环，Start/End 为片段区间，Fade 淡入淡出，Gain 为增益；Pitch 为半音移调，Reverse 反向播放。" y:y],y+=76;
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
    if(type==tri::FilterNode){[self label:@"Type  0 低通  1 高通  2 带通\nCutoff 20–20000 Hz，Resonance 为 Q 值。" y:y];y+=62;}
    if(type==tri::ReverbNode){[self label:@"Mix 干湿比，Size 空间大小，Damping 高频吸收，Width 立体声宽度。" y:y];y+=62;}
    if(type==tri::NoiseNode){[self label:@"Type  0 白噪声  1 粉红噪声。" y:y];y+=50;}
    if(type==tri::CompressorNode){[self label:@"Threshold 阈值，Ratio 压缩比，Attack/Release 包络，Makeup 补偿增益，Mix 干湿比。第二个 Audio 输入为侧链检测信号，可做闪避。" y:y];y+=74;}
    if(type==tri::LimiterNode){[self label:@"Threshold 开始限幅，Ceiling 输出上限，Release 恢复速度，Gain 输入增益。" y:y];y+=62;}
    if(type==tri::ChorusNode){[self label:@"Rate LFO 速度，Depth 调制深度，Mix 干湿比，Delay 基础延迟（毫秒）。" y:y];y+=62;}
    if(type==tri::QuantizeNode){[self label:@"Root 调性（0=C），Scale 0 大调 1 小调 2 多利亚 3 五声 4 半音，Mode 0 最近 1 向上 2 向下。" y:y];y+=70;}
    if(type==tri::TremoloNode){[self label:@"Rate 0.1–20 Hz，Depth 0–1，Mix 干湿比，Stereo 左右相位差。" y:y];y+=50;}
    if(type==tri::BitcrusherNode){[self label:@"Bits 1–16 位深，Rate 500–48000 Hz 采样保持，Mix 干湿比。" y:y];y+=50;}
    if(type==tri::PhaserNode){[self label:@"Rate LFO 速度，Depth 扫频深度，Feedback 反馈量，Mix 干湿比。" y:y];y+=50;}
    if(type==tri::ArpNode){[self label:@"Rhythm  0 直拍  1 Bossa Nova  2 Reggae  3 Disco  4 十六分" y:y];y+=70;}
    if(type==tri::DawClockNode){[self label:@"跟随 DAW 的 BPM、走带和定位。独立版可接收外部 MIDI Clock。" y:y];y+=70;}
    if(type==tri::DawOutputNode || type==tri::DawAudioNode){[self label:@"在 DAW 插件中连接对应音频总线。" y:y];y+=50;}
    if(type==tri::HardwareOutputNode){[self label:@"在独立版输出至系统选择的声卡。" y:y];y+=50;}
    if(type==tri::MidiOutNode){[self label:@"独立版发送至 Soundnet MIDI Out；AU 使用宿主的 MIDI 输出回调。通道 0 跟随输入，1–16 强制输出通道。" y:y];y+=75;}
    _inspector.frame=NSMakeRect(0,0,260,std::max(520.,y+20));
}
-(void)preset:(id)sender {
    NSTextField *field=[NSTextField new];field.stringValue=[[self node:_selected][@"type"] intValue]==tri::ChordNode?@"Cmaj7":@"Dm7 | G7 | Cmaj7";
    [self harmony:field];
}
-(void)exportMidi:(id)sender {
    NSSavePanel *panel=[NSSavePanel savePanel];panel.nameFieldStringValue=@"Soundnet.mid";
    panel.allowedContentTypes=@[[UTType typeWithFilenameExtension:@"mid"]];
    NSView *accessory=[[NSView alloc]initWithFrame:NSMakeRect(0,0,280,52)];
    NSTextField *label=[NSTextField labelWithString:@"导出时长（秒，1–120）"];
    label.frame=NSMakeRect(12,28,170,20);[accessory addSubview:label];
    NSTextField *secondsField=[[NSTextField alloc]initWithFrame:NSMakeRect(190,24,70,26)];
    secondsField.stringValue=@"8";[accessory addSubview:secondsField];
    panel.accessoryView=accessory;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result!=NSModalResponseOK)return;
        double seconds=std::max(1.0,std::min(120.0,secondsField.doubleValue));
        NSURL *url=panel.URL;NSDictionary *document=copyJSON(self.document);
        [self message:[NSString stringWithFormat:@"正在导出 %.1f 秒 MIDI…",seconds]];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
            tri::GraphSpec graph;NSString *why=nil;
            if(!triassets::decodeGraph(document,graph,&why)) {
                dispatch_async(dispatch_get_main_queue(),^{[self message:why ?: @"工作流无效，无法导出。"];});
                return;
            }
            tri::offline::Result rendered;std::string error;
            if(!tri::offline::render(graph,seconds,48000,rendered,error)) {
                NSString *text=[NSString stringWithUTF8String:error.c_str()];
                dispatch_async(dispatch_get_main_queue(),^{[self message:text];});
                return;
            }
            std::vector<tri::smf::TimedEvent> events;
            events.reserve(rendered.midi.size());
            for(const auto& e:rendered.midi)events.push_back({e.seconds,e.status,e.a,e.b});
            if(!tri::smf::writeMulti(url.path.UTF8String,events,seconds,rendered.bpm,error)) {
                NSString *text=[NSString stringWithUTF8String:error.c_str()];
                dispatch_async(dispatch_get_main_queue(),^{[self message:text];});
                return;
            }
            NSString *text=[NSString stringWithFormat:@"已导出 %lu 个 MIDI 事件 · %.1f 秒",(unsigned long)events.size(),seconds];
            dispatch_async(dispatch_get_main_queue(),^{[self message:text];});
        });
    }];
}
-(void)exportWav:(id)sender {
    NSSavePanel *panel=[NSSavePanel savePanel];panel.nameFieldStringValue=@"Soundnet.wav";
    panel.allowedContentTypes=@[[UTType typeWithFilenameExtension:@"wav"]];
    NSView *accessory=[[NSView alloc]initWithFrame:NSMakeRect(0,0,300,88)];
    NSTextField *label=[NSTextField labelWithString:@"导出时长（秒，1–120）"];
    label.frame=NSMakeRect(12,60,170,20);[accessory addSubview:label];
    NSTextField *secondsField=[[NSTextField alloc]initWithFrame:NSMakeRect(190,56,80,26)];
    secondsField.stringValue=@"8";[accessory addSubview:secondsField];
    NSTextField *rateLabel=[NSTextField labelWithString:@"采样率"];
    rateLabel.frame=NSMakeRect(12,30,60,20);[accessory addSubview:rateLabel];
    NSPopUpButton *ratePopup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(76,26,90,26)];
    [ratePopup addItemsWithTitles:@[@"44.1 kHz",@"48 kHz",@"96 kHz"]];[ratePopup selectItemAtIndex:1];[accessory addSubview:ratePopup];
    NSTextField *bitsLabel=[NSTextField labelWithString:@"位深"];
    bitsLabel.frame=NSMakeRect(180,30,40,20);[accessory addSubview:bitsLabel];
    NSPopUpButton *bitsPopup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(222,26,70,26)];
    [bitsPopup addItemsWithTitles:@[@"16-bit",@"24-bit"]];[bitsPopup selectItemAtIndex:0];[accessory addSubview:bitsPopup];
    panel.accessoryView=accessory;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result!=NSModalResponseOK)return;
        double seconds=std::max(1.0,std::min(120.0,secondsField.doubleValue));
        const double rates[]={44100,48000,96000};
        double rate=rates[std::max(0,std::min(2,(int)ratePopup.indexOfSelectedItem))];
        int bits=bitsPopup.indexOfSelectedItem==1?24:16;
        NSURL *url=panel.URL;NSDictionary *document=copyJSON(self.document);
        [self message:[NSString stringWithFormat:@"正在导出 %.1f 秒音频…",seconds]];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
            tri::GraphSpec graph;NSString *why=nil;
            if(!triassets::decodeGraph(document,graph,&why)) {
                dispatch_async(dispatch_get_main_queue(),^{[self message:why ?: @"工作流无效，无法导出。"];});
                return;
            }
            tri::offline::Result rendered;std::string error;
            if(!tri::offline::render(graph,seconds,rate,rendered,error)) {
                NSString *text=[NSString stringWithUTF8String:error.c_str()];
                dispatch_async(dispatch_get_main_queue(),^{[self message:text];});
                return;
            }
            if(!tri::wav::write(url.path.UTF8String,rendered.left,rendered.right,rendered.rate,error,bits)) {
                NSString *text=[NSString stringWithUTF8String:error.c_str()];
                dispatch_async(dispatch_get_main_queue(),^{[self message:text];});
                return;
            }
            double db=rendered.peak>0 ? 20*std::log10(rendered.peak) : -120;
            NSString *text=[NSString stringWithFormat:@"已导出 %.1f 秒 WAV · %.1f kHz / %d-bit · 峰值 %.1f dBFS",seconds,rendered.rate/1000.,bits,db];
            dispatch_async(dispatch_get_main_queue(),^{[self message:text];});
        });
    }];
}

-(void)exportStems:(id)sender {
    NSOpenPanel *panel=[NSOpenPanel openPanel];panel.canChooseDirectories=YES;panel.canChooseFiles=NO;
    panel.canCreateDirectories=YES;panel.prompt=@"选择文件夹";panel.message=@"选择分轨导出文件夹";
    NSView *accessory=[[NSView alloc]initWithFrame:NSMakeRect(0,0,300,88)];
    NSTextField *label=[NSTextField labelWithString:@"导出时长（秒，1–120）"];
    label.frame=NSMakeRect(12,60,170,20);[accessory addSubview:label];
    NSTextField *secondsField=[[NSTextField alloc]initWithFrame:NSMakeRect(190,56,80,26)];
    secondsField.stringValue=@"8";[accessory addSubview:secondsField];
    NSTextField *rateLabel=[NSTextField labelWithString:@"采样率"];
    rateLabel.frame=NSMakeRect(12,30,60,20);[accessory addSubview:rateLabel];
    NSPopUpButton *ratePopup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(76,26,90,26)];
    [ratePopup addItemsWithTitles:@[@"44.1 kHz",@"48 kHz",@"96 kHz"]];[ratePopup selectItemAtIndex:1];[accessory addSubview:ratePopup];
    NSTextField *bitsLabel=[NSTextField labelWithString:@"位深"];
    bitsLabel.frame=NSMakeRect(180,30,40,20);[accessory addSubview:bitsLabel];
    NSPopUpButton *bitsPopup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(222,26,70,26)];
    [bitsPopup addItemsWithTitles:@[@"16-bit",@"24-bit"]];[bitsPopup selectItemAtIndex:0];[accessory addSubview:bitsPopup];
    panel.accessoryView=accessory;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result!=NSModalResponseOK)return;
        double seconds=std::max(1.0,std::min(120.0,secondsField.doubleValue));
        const double rates[]={44100,48000,96000};
        double rate=rates[std::max(0,std::min(2,(int)ratePopup.indexOfSelectedItem))];
        int bits=bitsPopup.indexOfSelectedItem==1?24:16;
        NSURL *folder=panel.URL;NSDictionary *document=copyJSON(self.document);
        [self message:[NSString stringWithFormat:@"正在导出 %.1f 秒分轨…",seconds]];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
            tri::GraphSpec graph;NSString *why=nil;
            if(!triassets::decodeGraph(document,graph,&why)) {
                dispatch_async(dispatch_get_main_queue(),^{[self message:why ?: @"工作流无效，无法导出。"];});
                return;
            }
            std::vector<tri::offline::Stem> stems;std::string error;
            if(!tri::offline::renderStems(graph,seconds,rate,stems,error)) {
                NSString *text=[NSString stringWithUTF8String:error.c_str()];
                dispatch_async(dispatch_get_main_queue(),^{[self message:text];});
                return;
            }
            for(const auto& stem:stems) {
                std::string name="stem-"+std::to_string(stem.id)+".wav";
                std::string path=std::string(folder.fileSystemRepresentation)+"/"+name;
                if(!tri::wav::write(path,stem.result.left,stem.result.right,stem.result.rate,error,bits)) {
                    NSString *text=[NSString stringWithUTF8String:error.c_str()];
                    dispatch_async(dispatch_get_main_queue(),^{[self message:text];});
                    return;
                }
            }
            NSString *text=[NSString stringWithFormat:@"已导出 %lu 条分轨 · %.1f 秒 · %.1f kHz / %d-bit",(unsigned long)stems.size(),seconds,rate/1000.,bits];
            dispatch_async(dispatch_get_main_queue(),^{[self message:text];});
        });
    }];
}

-(void)applyPreset:(NSPopUpButton *)popup {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    auto presets=presetsFor([node[@"type"] intValue]);
    NSInteger index=popup.indexOfSelectedItem-1;
    if(index<0 || index>=(NSInteger)presets.size())return;
    NSDictionary *previous=copyJSON(_document);
    for(const auto& item:presets[index].values)
        node[@"parameters"][[@(item.first) stringValue]]=@(item.second);
    [self commit:previous];[self inspect];
    [self message:[NSString stringWithFormat:@"已应用预设：%s",presets[index].name]];
}
-(void)resetParams:(id)sender {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    NSDictionary *previous=copyJSON(_document);
    for(int p:controls([node[@"type"] intValue]))
        node[@"parameters"][[@(p) stringValue]]=@(tri::graphParamInfo(p).def);
    [self commit:previous];[self inspect];[self message:@"已恢复默认参数。"];
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
        [self.selection removeAllObjects];self.selected=0;self.selectedEdge=-1;
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
    [self message:[NSString stringWithFormat:@"已按信号深度排列 %lu 个节点",(unsigned long)nodes.count]];
}
-(void)blank:(id)sender {
    NSDictionary *previous=copyJSON(_document);
    _document=[@{@"version":@3,@"nodes":[NSMutableArray new],@"edges":[NSMutableArray new]}mutableCopy];
    _selected=0;[self commit:previous];[self inspect];
}
-(void)tick {
    NSDictionary *remote=_assetsRead ? _assetsRead()[@"graph"] : nil;
    if(remote && ![remote isEqual:_document] && !_moving && !_wireSource) {
        _document=copyJSON(remote);[self pruneSelection];[self inspect];_canvas.needsDisplay=YES;
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
double curveX(NSPoint a,NSPoint c1,NSPoint c2,NSPoint b,double t) {
    double u=1-t;return u*u*u*a.x+3*u*u*t*c1.x+3*u*t*t*c2.x+t*t*t*b.x;
}
double curveY(NSPoint a,NSPoint c1,NSPoint c2,NSPoint b,double t) {
    double u=1-t;return u*u*u*a.y+3*u*u*t*c1.y+3*u*t*t*c2.y+t*t*t*b.y;
}
bool nearEdge(NSDictionary *edge,NSDictionary *a,NSDictionary *b,NSPoint point,double tolerance) {
    if(!a || !b)return false;
    int port=[edge[@"port"] intValue],out=[edge[@"sourcePort"] intValue];
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
    if((self=[super initWithFrame:frame])) [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    return self;
}
-(BOOL)isFlipped{return YES;}
-(NSArray<NSURL *> *)draggedURLs:(id<NSDraggingInfo>)sender {
    return [sender.draggingPasteboard readObjectsForClasses:@[NSURL.class]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
}
-(NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    return [self draggedURLs:sender].count?NSDragOperationCopy:NSDragOperationNone;
}
-(NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender { return [self draggingEntered:sender]; }
-(BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSArray<NSURL *> *urls=[self draggedURLs:sender];
    if(!urls.count)return NO;
    NSPoint view=[self convertPoint:sender.draggingLocation fromView:nil];
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
    NSInteger edgeIndex=0;
    for(NSDictionary *edge in editor.document[@"edges"]) {
        NSDictionary *a=[editor node:[edge[@"source"] unsignedLongLongValue]],*b=[editor node:[edge[@"target"] unsignedLongLongValue]];
        int port=[edge[@"port"] intValue],out=[edge[@"sourcePort"] intValue];
        NSPoint from=jack(a,true,out),to=jack(b,false,port);
        NSBezierPath *path=[NSBezierPath new];[path moveToPoint:from];
        [path curveToPoint:to controlPoint1:NSMakePoint(from.x+70,from.y) controlPoint2:NSMakePoint(to.x-70,to.y)];
        BOOL chosen=(edgeIndex==editor.selectedEdge);
        [chosen?NSColor.whiteColor:signalColor(tri::outputType([a[@"type"] intValue],out)) setStroke];
        path.lineWidth=chosen?3:2;[path stroke];
        ++edgeIndex;
    }
    for(NSDictionary *node in editor.document[@"nodes"]) {
        int type=[node[@"type"] intValue];NSRect r=nodeRect(node);BOOL bypassed=[node[@"bypass"] boolValue];
        [[NSColor colorWithWhite:bypassed?.13:.18 alpha:1]setFill];[[NSBezierPath bezierPathWithRoundedRect:r xRadius:4 yRadius:4]fill];
        [[editor isSelected:[node[@"id"] unsignedLongLongValue]]?NSColor.whiteColor:[NSColor colorWithWhite:.4 alpha:1] setStroke];
        [[NSBezierPath bezierPathWithRect:r]stroke];
        drawText([NSString stringWithFormat:@"%@ #%@",nodeLabel(node),node[@"id"]],NSMakeRect(r.origin.x+9,r.origin.y+8,160,28),NSColor.whiteColor);
        if(bypassed)drawText(@"旁通",NSMakeRect(NSMaxX(r)-46,r.origin.y+8,40,20),NSColor.systemOrangeColor,10);
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
    for(NSString *group in @[@"输入",@"处理",@"逻辑",@"输出"]) {
        NSMenuItem *category=[menu addItemWithTitle:group action:nil keyEquivalent:@""];
        category.submenu=[NSMenu new];
        for(NSArray *row in catalog())if([row[2] isEqual:group]) {
            NSMenuItem *item=[category.submenu addItemWithTitle:row[1] action:@selector(insert:) keyEquivalent:@""];
            item.target=editor;item.tag=[row[0] integerValue];
        }
    }
    [menu addItem:[NSMenuItem separatorItem]];
    BOOL hasSelection=[editor activeIDs].count>0;
    for(NSArray *item in @[@[@"复制到剪贴板",@"copy:",@"c"],@[@"剪切",@"cut:",@"x"],@[@"粘贴节点",@"paste:",@"v"],
                           @[@"复制并偏移",@"duplicate:",@""],@[@"断开选中节点",@"disconnect:",@""],@[@"删除选中节点",@"remove:",@""]]) {
        NSMenuItem *action=[menu addItemWithTitle:item[0] action:NSSelectorFromString(item[1]) keyEquivalent:item[2]];
        action.target=editor;
        action.enabled=[item[1] isEqualToString:@"paste:"]?editor.clipboard!=nil:hasSelection;
    }
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *all=[menu addItemWithTitle:@"选择全部" action:@selector(selectAll:) keyEquivalent:@"a"];
    all.target=editor;all.enabled=[(NSArray *)editor.document[@"nodes"] count]>0;
    [NSMenu popUpContextMenu:menu withEvent:event forView:self];
}
-(void)mouseDown:(NSEvent *)event {
    SNInstanceEditor *editor=_editor;[self.window makeFirstResponder:self];
    NSPoint view=[self convertPoint:event.locationInWindow fromView:nil],point=[editor world:view];
    editor.lastPoint=view;editor.dragStart=copyJSON(editor.document);
    BOOL additive=(event.modifierFlags&(NSEventModifierFlagShift|NSEventModifierFlagCommand))!=0;
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
            [editor message:@"已选中连线，按 Delete 删除。"];
            self.needsDisplay=YES;return;
        }
        ++index;
    }
    if(additive) {
        editor.boxSelecting=YES;editor.boxOrigin=point;editor.boxCurrent=point;
        self.needsDisplay=YES;return;
    }
    [editor selectNone:nil];editor.panning=YES;
}
-(void)mouseDragged:(NSEvent *)event {
    SNInstanceEditor *editor=_editor;
    NSPoint view=[self convertPoint:event.locationInWindow fromView:nil];
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
    if(event.modifierFlags&NSEventModifierFlagCommand) _editor.zoom=std::clamp(_editor.zoom*std::exp(-event.scrollingDeltaY*.02),.05,3.);
    else _editor.origin=NSMakePoint(_editor.origin.x-event.scrollingDeltaX,_editor.origin.y-event.scrollingDeltaY);
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
    editor.selection=[NSMutableSet new];editor.selectedEdge=-1;editor.snap=NO;
    [editor button:@"打开" frame:NSMakeRect(12,10,65,28) action:@selector(open:) parent:editor];
    [editor button:@"保存" frame:NSMakeRect(82,10,65,28) action:@selector(save:) parent:editor];
    [editor button:@"撤销" frame:NSMakeRect(152,10,65,28) action:@selector(undo:) parent:editor];
    [editor button:@"重做" frame:NSMakeRect(222,10,65,28) action:@selector(redo:) parent:editor];
    [editor button:@"适配" frame:NSMakeRect(292,10,65,28) action:@selector(fit:) parent:editor];
    [editor button:@"清空" frame:NSMakeRect(362,10,65,28) action:@selector(blank:) parent:editor];
    [editor button:@"停音" frame:NSMakeRect(432,10,65,28) action:@selector(panic:) parent:editor];
    [editor button:@"复制" frame:NSMakeRect(502,10,65,28) action:@selector(copy:) parent:editor];
    [editor button:@"粘贴" frame:NSMakeRect(572,10,65,28) action:@selector(paste:) parent:editor];
    [editor button:@"查找" frame:NSMakeRect(642,10,65,28) action:@selector(search:) parent:editor];
    NSButton *snapButton=[editor button:@"吸附" frame:NSMakeRect(712,10,65,28) action:@selector(snap:) parent:editor];
    [snapButton setButtonType:NSButtonTypePushOnPushOff];
    [editor button:@"导出" frame:NSMakeRect(782,10,65,28) action:@selector(exportWav:) parent:editor];
    [editor button:@"排列" frame:NSMakeRect(852,10,65,28) action:@selector(autoLayout:) parent:editor];
    NSTextField *heading=[NSTextField labelWithString:@"Soundnet 2.36    输入 → 处理 → 输出"];
    heading.frame=NSMakeRect(930,15,200,25);[editor addSubview:heading];
    SNInstanceCanvas *canvas=[[SNInstanceCanvas alloc]initWithFrame:NSMakeRect(8,48,862,505)];
    canvas.editor=editor;editor.canvas=canvas;[editor addSubview:canvas];
    NSScrollView *scroll=[[NSScrollView alloc]initWithFrame:NSMakeRect(878,48,260,505)];
    scroll.hasVerticalScroller=YES;editor.inspectorScroll=scroll;
    editor.inspector=[[SNFlippedView alloc]initWithFrame:NSMakeRect(0,0,260,520)];
    scroll.documentView=editor.inspector;[editor addSubview:scroll];
    editor.status=[NSTextField labelWithString:@"右键插入节点 · 拖入 MIDI/WAV/AIFF/MLMODEL · Shift 框选 · Cmd+C/V"];
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
