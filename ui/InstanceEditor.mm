#import "TriEditor.h"
#import "../platform/ProjectDocument.hpp"
#include "../platform/ProjectEnvironment.hpp"
#include "../nodes/GeneratedEditors.hpp"
#import "SNLocalization.h"
#include "CanvasZones.hpp"
#include <atomic>
#include <memory>
#include <optional>
#include <string_view>
#import "../src/WorkflowAssets.h"
#import "../src/OfflineRender.hpp"
#include "../platform/TaskService.hpp"
#import "../src/WavFile.hpp"
#import "../src/MidiFileWriter.hpp"
#include "../src/StandardMidiFile.hpp"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <QuartzCore/QuartzCore.h>
#include <cmath>
#include <cstring>

namespace {
using soundnet::canvas::Stage;
using soundnet::canvas::Zones;
using soundnet::canvas::stageCount;
constexpr CGFloat cardWidth=172, zoneHeaderHeight=28;

bool isMediaPort(const tri::PortSpec& port) {
    return port.signal==tri::Signal::Midi || port.signal==tri::Signal::Audio;
}
bool isFixedAudioClip(const tri::NodeSpec& spec) {
    return spec.assetKind && std::strcmp(spec.assetKind,"audio")==0 &&
        spec.timeMode==tri::TimeMode::Timed && !(spec.capabilities&tri::CapTask) &&
        std::none_of(spec.inputs.begin(),spec.inputs.end(),isMediaPort);
}
Stage canvasStage(int type) {
    const auto* spec=tri::nodeSpec(type);
    if(!spec || spec->environmentRole!=tri::EnvironmentRole::None)return Stage::Free;
    if(spec->capabilities&tri::CapOutputSink)return Stage::Output;
    const std::string_view category=spec->category;
    if(category=="音符" || isFixedAudioClip(*spec))return Stage::Notes;
    if(category=="发声")return Stage::Sources;
    if(category=="声音处理")return Stage::Effects;
    if(category=="走带与控制")return Stage::Free;

    // Existing descriptors also cover SDK 2 packages with custom categories.
    const bool mediaIn=std::any_of(spec->inputs.begin(),spec->inputs.end(),isMediaPort);
    if(std::any_of(spec->inputs.begin(),spec->inputs.end(),[](const auto& port){return port.role==tri::RoleOutput;}))
        return Stage::Output;
    if(category=="工程与 I/O" || category=="输入" || category=="输出") {
        if(mediaIn && category!="输入")return Stage::Output;
        if(!mediaIn && std::any_of(spec->outputs.begin(),spec->outputs.end(),isMediaPort) &&
           !spec->assetKind && !(spec->capabilities&tri::CapTask))return Stage::Notes;
    }
    if(std::any_of(spec->outputs.begin(),spec->outputs.end(),[](const auto& port){return port.signal==tri::Signal::Audio;}))
        return std::any_of(spec->inputs.begin(),spec->inputs.end(),[](const auto& port){return port.signal==tri::Signal::Audio;})
            ? Stage::Effects : Stage::Sources;
    if(std::any_of(spec->outputs.begin(),spec->outputs.end(),[](const auto& port){return port.signal==tri::Signal::Midi;}))
        return Stage::Notes;
    return Stage::Free;
}

const tri::NodeSpec *fileDropSpec(NSURL *url,BOOL clipMode) {
    NSString *extension=url.pathExtension.lowercaseString;
    const tri::NodeSpec *chosen=nullptr;
    for(const auto& candidate:tri::nodeSpecs()) {
        if(!candidate.assetKind||candidate.assetDropPriority<=0)continue;
        const bool accepts=std::any_of(candidate.importExtensions.begin(),candidate.importExtensions.end(),
            [&](const char *ext){return [extension isEqualToString:@(ext)];});
        if(accepts && (!chosen || (clipMode?candidate.assetDropPriority>chosen->assetDropPriority:
            candidate.assetDropPriority<chosen->assetDropPriority)))chosen=&candidate;
    }
    return chosen;
}

NSArray<NSString *> *creationCategories() {return @[@"音符",@"发声",@"声音处理"];}
NSArray<NSString *> *utilityCategories() {return @[@"走带与控制",@"工程与 I/O"];}
NSArray<NSString *> *libraryCategories(const std::vector<int>& types) {
    NSMutableOrderedSet<NSString *> *categories=[NSMutableOrderedSet orderedSetWithArray:creationCategories()];
    [categories addObjectsFromArray:utilityCategories()];
    for(int type:types)[categories addObject:@(tri::nodeCategory(type))];
    return categories.array;
}
NSString *librarySection(NSString *category) {
    if([creationCategories() containsObject:category])return @"创作节点";
    if([utilityCategories() containsObject:category])return @"辅助节点";
    return @"其他节点";
}
NSColor *libraryTint(NSString *section) {
    if([section isEqualToString:@"创作节点"])return [NSColor colorWithSRGBRed:.40 green:.75 blue:.94 alpha:1];
    if([section isEqualToString:@"辅助节点"])return [NSColor colorWithSRGBRed:.90 green:.70 blue:.42 alpha:1];
    return NSColor.lightGrayColor;
}
NSString *title(int type) { return SNNodeLocalized(type,@(tri::nodeTitle(type))); }
// v4 documents identify a node by `nodeKey` and a parameter by its stable key.
// A missing or unsupported node version stays an inert placeholder in the UI,
// just as it does in the compiler; its saved fields remain intact.
int typeOf(NSDictionary *node) {
    id key=node[@"nodeKey"];
    const auto* spec=[key isKindOfClass:NSString.class]?tri::nodeSpecForKey([key UTF8String]):tri::nodeSpec([node[@"type"] intValue]);
    if(!spec)return 0;
    const int version=node[@"nodeVersion"]?[node[@"nodeVersion"] intValue]:1;
    const int state=node[@"stateVersion"]?[node[@"stateVersion"] intValue]:1;
    return version==spec->nodeVersion&&state==spec->stateVersion?spec->type:0;
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
static NSArray<NSString*> *paramOptions(int type,int p) {
    // Enumerations live in the descriptor: one list for validation, storage and
    // the inspector.
    const tri::ParamSpec *spec=tri::paramSpec(p);
    if(!spec||!spec->options||!spec->optionCount)return nil;
    NSMutableArray *rows=[NSMutableArray arrayWithCapacity:spec->optionCount];
    for(int i=0;i<spec->optionCount;++i)
        rows[i]=SNNodeLocalized(type,[NSString stringWithUTF8String:spec->options[i]]);
    return rows;
}
NSString *pitchName(int note) {
    static NSString *const names[]={@"C",@"C♯",@"D",@"E♭",@"E",@"F",@"F♯",@"G",@"A♭",@"A",@"B♭",@"B"};
    return [NSString stringWithFormat:@"%@%d",names[note%12],note/12-1];
}
// Port names come from the descriptor, so a node that renames or adds a port
// never needs an editor change. The signal is only a fallback for unknown types.
NSString *portLabel(int type,bool input,int port) {
    const tri::PortSpec *spec=tri::nodePort(type,input,size_t(port));
    if(spec&&spec->label&&*spec->label)return SNNodeLocalized(type,@(spec->label));
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
NSDictionary *taskInputSnapshot(NSDictionary *node) {
    return @{@"nodeKey":node[@"nodeKey"] ?: @"",@"nodeVersion":node[@"nodeVersion"] ?: @1,
        @"parameters":node[@"parameters"] ?: @{},@"texts":node[@"texts"] ?: @{},
        @"asset":node[@"asset"] ?: @{},@"state":node[@"state"] ?: @{},
        @"resources":node[@"resources"] ?: @{},@"resourceCollections":node[@"resourceCollections"] ?: @{},
        @"textResourceTypes":node[@"textResourceTypes"] ?: @{}};
}
std::vector<int> controls(int type) { return tri::nodeParamList(type); }
NSString *parameterName(int type,int p) {return SNNodeLocalized(type,@(tri::graphParamInfo(p).name));}
NSString *projectFolder(NSDictionary *document) {
    for(NSDictionary *node in document[@"nodes"]) {
        const auto* spec=tri::nodeSpec(typeOf(node));
        if(spec&&spec->environmentRole==tri::EnvironmentRole::ProjectSettings) {
            id folder=node[@"texts"][@"project.folder"];return [folder isKindOfClass:NSString.class]?folder:@"";
        }
    }
    return @"";
}
void setProjectFolder(NSMutableDictionary *document,NSString *folder) {
    for(NSMutableDictionary *node in document[@"nodes"]) {
        const auto* spec=tri::nodeSpec(typeOf(node));
        if(spec&&spec->environmentRole==tri::EnvironmentRole::ProjectSettings) {
            NSMutableDictionary *texts=[node[@"texts"] mutableCopy] ?: [NSMutableDictionary new];
            texts[@"project.folder"]=folder ?: @"";node[@"texts"]=texts;return;
        }
    }
}
const std::vector<tri::NodePreset>& presetsFor(int type) {
    static const std::vector<tri::NodePreset> empty;const auto* spec=tri::nodeSpec(type);return spec?spec->presets:empty;
}
void applyNodeContent(NSMutableDictionary *node,NSDictionary *replacement) {
    for(NSString *key in @[@"parameters",@"texts",@"asset",@"state",@"resources",@"resourceCollections",@"textResourceTypes"]) {
        id value=replacement[key];if(value==NSNull.null)[node removeObjectForKey:key];else if(value)node[key]=copyJSON(value);
    }
}

}

namespace { NSRect nodeRect(NSDictionary *node); }

static NSString *const SNNodeTypePasteboard = @"com.soundnet.node-type";

@class SNInstanceEditor;
@interface SNInstanceCanvas : NSView <NSMenuDelegate>
@property(weak) SNInstanceEditor *editor;
-(BOOL)prepareInsertionOfType:(int)type at:(NSPoint *)point expand:(BOOL)expand
                        starts:(NSMutableDictionary<NSNumber *,NSValue *> *)starts;
-(void)invalidateLayout;
-(void)animatePlacements:(NSDictionary<NSNumber *,NSValue *> *)starts focus:(uint64_t)focus;
-(void)clearPlacementFeedback;
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

@interface SNInstanceEditor : NSView <NSTextFieldDelegate> {
    BOOL _autosaveDirty;
    uint64_t _midiCaptureToken,_midiCaptureNode;
    std::unique_ptr<soundnet::TaskService> taskService;
    std::map<uint64_t,uint64_t> taskInputRevisions;
    std::map<uint64_t,tri::NodeTaskApply> taskApplications;
    uint64_t nextTaskInputRevision;
    NSMutableDictionary<NSNumber *,NSDictionary *> *taskInputs;
}
@property(copy) TRIRead read;
@property(copy) TRIWrite write;
@property(copy) TRIMIDI midi;
@property(copy) TRINodeStop nodeStop;
@property(copy) TRIInputRead inputRead;
@property(copy) TRIInputWrite inputWrite;
@property(copy) TRIAssetsRead assetsRead;
@property(copy) TRIAssetsWrite assetsWrite;
@property(copy) TRIEnvironmentRead environmentRead;
@property(copy) TRIMidiCapture midiCapture;
@property BOOL hostManaged;
@property(copy) TRIProcessingRead processingRead;
@property(copy) TRIProcessingWrite processingWrite;
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
@property uint64_t playbackEpoch;
@property uint64_t catalogGeneration;
@property(strong) NSMutableDictionary *document;
@property(strong) NSURL *projectURL;
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
@property NSPoint wireCursor;
@property uint64_t moving;
@property BOOL panning;
@property NSPoint lastPoint;
@property(strong) NSTimer *timer;
@property(strong) dispatch_queue_t autosaveQueue;
-(void)autosave;
-(void)flushAutosave;
-(void)finishAutosave;
-(BOOL)prepareForNodeRefresh:(NSString **)why;
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
-(void)textChanged:(NSTextField *)field;
-(void)beginTaskSession;
-(void)cancelMidiCapture;
-(void)reconcileBackgroundTasks;
-(void)drainBackgroundTasks;
-(void)configureTaskContext:(tri::NodeEditorContext *)context forNode:(uint64_t)uid;
-(tri::NodeEditorContext)editorContextForNode:(uint64_t)uid;
-(BOOL)commit:(NSDictionary *)previous;
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


@implementation SNInstanceEditor
-(void)drawRect:(NSRect)dirty {
    [[NSColor colorWithCalibratedWhite:.105 alpha:1] setFill];NSRectFill(self.bounds);
    [[NSColor colorWithCalibratedWhite:.14 alpha:1] setFill];NSRectFill(NSMakeRect(0,0,NSWidth(self.bounds),92));
    [NSColor.separatorColor setFill];NSRectFill(NSMakeRect(12,91,NSWidth(self.bounds)-24,1));
}
-(BOOL)isFlipped{return YES;}
-(void)dealloc {[_timer invalidate];[[NSNotificationCenter defaultCenter] removeObserver:self];if(_exportCancellation)_exportCancellation->store(true);if(taskService)taskService->shutdown();[self cancelMidiCapture];}
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
-(BOOL)commit:(NSDictionary *)previous {
    tri::GraphSpec graph;NSString *why=nil;
    std::vector<tri::Diagnostic> diagnostics;
    BOOL applied=NO;
    if(!triassets::decodeGraph(_document,graph,&why,false,true,&diagnostics) ||
       !_assetsWrite || !_assetsWrite(@{@"graph":_document})) {
        _document=copyJSON(previous) ?: tri::newProjectDocument();[self message:why ?: SNLocalized(@"宿主无法应用节点图。")];
    } else {
        applied=YES;
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
    [self reconcileBackgroundTasks];
    [self pruneSelection];
    [_canvas invalidateLayout];
    _canvas.needsDisplay=YES;
    return applied;
}
-(void)autosave {
    if(getenv("SOUNDNET_NO_AUTOSAVE"))return;
    if(self.hostManaged)return;
    _autosaveDirty=YES;
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(flushAutosave) object:nil];
    [self performSelector:@selector(flushAutosave) withObject:nil afterDelay:0.75];
}
-(void)flushAutosave {
    if(!_autosaveDirty)return;
    _autosaveDirty=NO;
    NSURL *url=triassets::autosaveURL();if(!url)return;
    if(!_autosaveQueue)_autosaveQueue=dispatch_queue_create("com.soundnet.autosave",DISPATCH_QUEUE_SERIAL);
    NSDictionary *snapshot=copyJSON(_document);
    __weak SNInstanceEditor *weak=self;
    dispatch_async(_autosaveQueue, ^{
        NSString *why=nil;
        if(!triassets::writeProject(snapshot,url,&why))
            dispatch_async(dispatch_get_main_queue(),^{[weak message:why];});
    });
}
-(void)finishAutosave {
    [self flushAutosave];
    if(_autosaveQueue)dispatch_sync(_autosaveQueue,^{});
}
-(BOOL)prepareForNodeRefresh:(NSString **)why {
    BOOL busy=_exportBusy||_midiCaptureToken||self.window.attachedSheet;
    if(taskService)for(NSNumber *uid in taskInputs) {
        soundnet::TaskService::Snapshot snapshot;
        if(taskService->snapshot(uid.unsignedLongLongValue,snapshot)&&snapshot.active()) {busy=YES;break;}
    }
    if(busy) {
        if(why)*why=SNLocalized(@"请先完成录制、生成或导出，再刷新节点。");
        return NO;
    }
    [self.window makeFirstResponder:_canvas];
    [self drainBackgroundTasks];
    [self finishAutosave];
    return triassets::writeProject(_document,triassets::autosaveURL(),why);
}
-(void)restoreAutosave:(id)sender {
    NSURL *url=triassets::autosaveURL();
    if(!url||![NSFileManager.defaultManager fileExistsAtPath:url.path]) {
        [self message:SNLocalized(@"没有可恢复的自动保存")];return;
    }
    tri::GraphSpec graph;NSString *why=nil;
    NSDictionary *object=triassets::readProject(url,&why);
    NSDictionary *canonical=object?triassets::canonicalGraph(object,&why):nil;
    if(!canonical || !triassets::decodeGraph(canonical,graph,&why,true,true)) {
        [self message:why ?: SNLocalized(@"自动保存文件无效")];return;
    }
    NSDictionary *previous=copyJSON(_document);
    [self beginTaskSession];
    _document=copyJSON(canonical);
    [_selection removeAllObjects];_selected=0;_selectedEdge=-1;
    [self commit:previous];[self inspect];[self fit:nil];
    [self message:SNLocalized(@"已恢复自动保存")];
}
-(void)pruneSelection {
    if(_midiCaptureToken&&![self node:_midiCaptureNode])[self cancelMidiCapture];
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
-(NSMutableDictionary *)makeNode:(int)type at:(NSPoint)point id:(uint64_t)uid expand:(BOOL)expand
                           starts:(NSMutableDictionary<NSNumber *,NSValue *> *)starts {
    const tri::NodeSpec *spec=tri::nodeSpec(type);
    if(!spec||!spec->instantiable)return nil;
    NSPoint destination=point;
    if(![_canvas prepareInsertionOfType:type at:&destination expand:expand starts:starts]) {
        [self message:SNLocalized(@"区域太窄，按住 Shift 放置可展开。")];
        return nil;
    }
    if(!NSEqualPoints(point,destination))starts[@(uid)]=[NSValue valueWithPoint:point];
    NSMutableDictionary *parameters=[NSMutableDictionary new];
    for(int p:controls(type))parameters[paramKeyName(p)]=@(tri::graphParamInfo(p).def);
    NSMutableDictionary *node=[@{@"id":@(uid),
        @"nodeKey":[NSString stringWithUTF8String:spec->key],
        @"nodeVersion":@(spec->nodeVersion),
        @"stateVersion":@(spec->stateVersion),
        @"x":@(destination.x),@"y":@(destination.y),@"parameters":parameters} mutableCopy];
    [_document[@"nodes"] addObject:node];
    [_canvas invalidateLayout];
    return node;
}
-(void)insert:(NSMenuItem *)sender {
    [self insertNodeType:int(sender.tag) atPoint:_menuPoint];
}
-(BOOL)dropFileURLs:(NSArray<NSURL *> *)urls atPoint:(NSPoint)point clipMode:(BOOL)clipMode {
    if(!urls.count)return NO;
    if(_exportBusy){[self message:SNLocalized(@"已有处理正在进行，请等待或取消。")];return NO;}
    NSMutableArray<NSDictionary *> *jobs=[NSMutableArray new];
    NSMutableArray<NSString *> *errors=[NSMutableArray new];
    for(NSURL *url in urls) {
        NSString *extension=url.pathExtension.lowercaseString;
        const auto *chosen=fileDropSpec(url,clipMode);
        if(!chosen){[errors addObject:[NSString stringWithFormat:SNLocalized(@"%@：不支持的格式"),url.lastPathComponent]];continue;}
        NSString *kind=@(chosen->assetKind);const int type=chosen->type;
        [jobs addObject:@{@"url":url,@"kind":kind,@"type":@(type)}];
    }
    if(!jobs.count) {
        if(errors.count)[self message:errors.firstObject];
        NSBeep();return NO;
    }
    if(![self beginExport])return NO;
    const BOOL expand=(NSEvent.modifierFlags & NSEventModifierFlagShift)!=0;
    auto cancelled=_exportCancellation;
    NSDictionary *targetDocument=_document;
    auto catalog=tri::nodeCatalogSnapshot();
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
                if(cancelled->load()||tri::nodeCatalogSnapshot()!=catalog){[self finishExport:SNLocalized(@"已取消素材载入。")];return;}
                // Opening, restoring or undoing to another document invalidates
                // this drop. Edits in the same document remain intact.
                if(self.document!=targetDocument){[self finishExport:SNLocalized(@"节点已改变，未应用素材。")];return;}
                if(!imported.count){[self finishExport:errors.firstObject ?: SNLocalized(@"无法读取")];return;}
                NSDictionary *previous=copyJSON(self.document);
                uint64_t next=1,last=0;NSUInteger offset=0;
                NSMutableDictionary<NSNumber *,NSValue *> *starts=[NSMutableDictionary new];
                for(NSDictionary *node in self.document[@"nodes"])next=std::max(next,[node[@"id"] unsignedLongLongValue]+1);
                for(NSDictionary *result in imported) {
                    const uint64_t uid=next++;int type=[result[@"type"] intValue];
                    const NSPoint requested=NSMakePoint(point.x+offset*30,point.y+offset*30);
                    NSMutableDictionary *node=[self makeNode:type at:requested id:uid expand:expand starts:starts];
                    if(!node){[errors addObject:self.status.stringValue];continue;}
                    node[@"asset"]=result[@"asset"];
                    if(const auto* spec=tri::nodeSpec(type))for(const auto& value:spec->importParameters)setParam(node,value.first,value.second);
                    last=uid;++offset;
                }
                if(!offset){[self finishExport:errors.firstObject];return;}
                self.selected=last;[self.selection removeAllObjects];[self.selection addObject:@(last)];
                if([self commit:previous])[self.canvas animatePlacements:starts focus:offset==1?last:0];
                [self inspect];
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
    NSMutableSet *removed=[NSMutableSet setWithArray:ids];
    if([_document[@"settings"][@"environmentRequired"] boolValue])
        for(NSDictionary* node in _document[@"nodes"])if(tri::isEnvironmentNode(node))[removed removeObject:node[@"id"]];
    if(!removed.count){[self message:SNLocalized(@"工程环境节点不能删除。")];return;}
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
        if(tri::isEnvironmentNode(original)) {mapping[key]=key;continue;}
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
    const NSUInteger before=[_document[@"nodes"] count];
    [self copy:nil];[self remove:nil];
    const NSUInteger after=[_document[@"nodes"] count];
    if(after<before)[self message:[NSString stringWithFormat:SNLocalized(@"已剪切 %lu 个节点。"),(unsigned long)(before-after)]];
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
        if(tri::isEnvironmentNode(node)) {
            // Environment entries in the clipboard are references. Reconnect
            // their wires to this project's role without duplicating its nodes.
            const auto* environment=tri::nodeSpecForKey([node[@"nodeKey"] UTF8String]);
            if(!environment)continue;
            const auto role=environment->environmentRole;
            for(NSDictionary* existing in _document[@"nodes"]) {
                const auto* descriptor=tri::nodeSpec(typeOf(existing));
                if(descriptor&&descriptor->environmentRole==role) {mapping[node[@"id"]]=existing[@"id"];break;}
            }
            continue;
        }
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
    _selectedEdge=-1;BOOL applied=[self commit:previous];[self inspect];
    if(applied)[self message:[NSString stringWithFormat:SNLocalized(@"已粘贴 %lu 个节点。"),(unsigned long)copies.count]];
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
        [self reconcileBackgroundTasks];
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
    NSMutableDictionary *node=[self node:_selected];const auto* descriptor=tri::nodeSpec(typeOf(node));
    if(!node||!descriptor||descriptor->triggerParameter<0)return;
    if(descriptor->triggerStarts)[self resumeProcessing];
    NSDictionary *previous=copyJSON(_document);const int parameter=descriptor->triggerParameter;
    const auto& info=tri::graphParamInfo(parameter);
    const double value=paramValue(node,parameter).doubleValue;
    setParam(node,parameter,value>=info.max?info.min:value+1);
    [self commit:previous];[self inspect];
}
-(void)bypassChanged:(NSButton *)sender {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    NSDictionary *previous=copyJSON(_document);
    if(sender.state==NSControlStateValueOn)node[@"bypass"]=@YES;
    else [node removeObjectForKey:@"bypass"];
    [self commit:previous];[self inspect];
}
-(void)resumeProcessing {
    if(_processingWrite)_processingWrite(YES);
    _processingActive=YES;[self updateProcessingControls];
}
-(void)playGraph:(id)sender {
    [self resumeProcessing];BOOL changed=NO;
    for(NSMutableDictionary *node in _document[@"nodes"]) {
        const auto* descriptor=tri::nodeSpec(typeOf(node));if(!descriptor||!descriptor->triggerStarts||descriptor->triggerParameter<0)continue;
        const int parameter=descriptor->triggerParameter;const auto& info=tri::graphParamInfo(parameter);
        const double value=paramValue(node,parameter).doubleValue;setParam(node,parameter,value>=info.max?info.min:value+1);changed=YES;
    }
    if(changed)[self commit:nil];else [self message:SNLocalized(@"处理已恢复，可演奏 MIDI 或接入音频。")];
}
-(void)stopGraph:(id)sender {
    ++_playbackEpoch;
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
    NSString *caption=SNLocalized(active?@"● 处理中":@"■ 已停止");
    if(![_processingLabel.stringValue isEqualToString:caption])_canvas.needsDisplay=YES;
    _processingLabel.stringValue=caption;
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
-(void)import:(id)sender {
    if(_exportBusy){[self message:SNLocalized(@"已有处理正在进行，请等待或取消。")];return;}
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    uint64_t uid=_selected;
    const auto* descriptor=tri::nodeSpec(typeOf(node));if(!descriptor||!descriptor->assetKind)return;
    NSString *kind=@(descriptor->assetKind);
    auto catalog=tri::nodeCatalogSnapshot();
    NSOpenPanel *panel=[NSOpenPanel openPanel];panel.canChooseDirectories=[kind isEqual:@"model"];
    NSMutableArray *extensions=[NSMutableArray new];for(const char* ext:descriptor->importExtensions)[extensions addObject:@(ext)];
    if(!extensions.count)[extensions addObjectsFromArray:[kind isEqual:@"audio"]?@[@"wav",@"aif",@"aiff",@"mp3",@"m4a",@"aac",@"caf",@"flac"]:[kind isEqual:@"clip"]?@[@"mid",@"midi"]:@[@"json"]];
    NSMutableArray *types=[NSMutableArray new];
    for(NSString *ext in extensions) {
        UTType *type=[UTType typeWithFilenameExtension:ext];if(type)[types addObject:type];
    }
    panel.allowedContentTypes=types;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result!=NSModalResponseOK)return;
        if(tri::nodeCatalogSnapshot()!=catalog)return;
        if(![self beginExport])return;
        auto cancelled=self.exportCancellation;
        [self message:SNLocalized(@"正在载入素材…")];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
            NSString *why=nil;NSDictionary *asset=triassets::importAsset(kind,panel.URL,&why);
            dispatch_async(dispatch_get_main_queue(),^{
                if(cancelled->load()||tri::nodeCatalogSnapshot()!=catalog){[self finishExport:SNLocalized(@"已取消素材载入。")];return;}
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
-(void)toggleParameterGroup:(NSButton *)sender {
    if([_collapsedParameterGroups containsObject:sender.identifier])[_collapsedParameterGroups removeObject:sender.identifier];
    else [_collapsedParameterGroups addObject:sender.identifier];
    [self inspect];
}
-(tri::NodeEditorContext)editorContextForNode:(uint64_t)targetID {
    tri::NodeEditorContext context;
    NSMutableDictionary *node=[self node:targetID];const int type=typeOf(node);const auto* descriptor=tri::nodeSpec(type);
    if(!node||!descriptor)return context;
    context.configuration=copyJSON(node);context.environment=_environmentRead?_environmentRead():@{};
    context.localize=^NSString*(NSString* text){return SNNodeLocalized(type,text);};
    __weak SNInstanceEditor* weak=self;NSDictionary* targetDocument=_document;NSDictionary* targetNode=node;
    auto catalog=tri::nodeCatalogSnapshot();
    for(const auto& module:catalog->modules)if(module.spec.type==type) {
        context.resourceDirectory=[NSString stringWithUTF8String:module.resourceDirectory.c_str()];break;
    }
    bool (^current)(void)=^bool {
        SNInstanceEditor* strong=weak;
        return NSThread.isMainThread&&strong&&strong.document==targetDocument&&[strong node:targetID]==targetNode&&tri::nodeCatalogSnapshot()==catalog;
    };
    context.isCurrent=current;
    context.commit=^(NSDictionary* replacement) {
        if(!current()||![replacement isKindOfClass:NSDictionary.class])return;
        SNInstanceEditor* strong=weak;NSDictionary* previous=copyJSON(strong.document);
        applyNodeContent([strong node:targetID],replacement);[strong commit:previous];[strong inspect];
    };
    context.createNode=^bool(NSString* key,NSDictionary* configuration) {
        if(!current()||![key isKindOfClass:NSString.class]||![configuration isKindOfClass:NSDictionary.class])return false;
        const auto* target=tri::nodeSpecForKey(key.UTF8String);if(!target||!target->instantiable)return false;
        SNInstanceEditor* strong=weak;
        if(target->environmentRole!=tri::EnvironmentRole::None)for(NSDictionary* row in strong.document[@"nodes"]) {
            const auto* spec=tri::nodeSpec(typeOf(row));if(spec&&spec->environmentRole==target->environmentRole)return false;
        }
        NSDictionary* previous=copyJSON(strong.document);uint64_t uid=1;
        for(NSDictionary* row in strong.document[@"nodes"])uid=std::max(uid,[row[@"id"] unsignedLongLongValue]+1);
        NSMutableDictionary *starts=[NSMutableDictionary new];
        NSMutableDictionary* created=[strong makeNode:target->type at:NSMakePoint([targetNode[@"x"] doubleValue]+30,[targetNode[@"y"] doubleValue]+150) id:uid expand:NO starts:starts];
        if(!created)return false;
        applyNodeContent(created,configuration);
        const bool committed=[strong commit:previous];
        if(committed) {
            [strong.canvas animatePlacements:starts focus:uid];
            [strong selectOnly:uid];
        }
        return committed;
    };
    __block uint64_t auditionEpoch=_playbackEpoch;
    context.resumePlayback=^{if(!current())return;SNInstanceEditor* strong=weak;[strong resumeProcessing];auditionEpoch=strong.playbackEpoch;};
    context.stopPlayback=^{if(current())[weak stopGraph:nil];};
    context.sendMidi=^(uint8_t status,uint8_t a,uint8_t b) {
        if(!current())return;SNInstanceEditor* strong=weak;
        const bool starts=(status&0xf0)==0x90&&b;
        if(strong.playbackEpoch!=auditionEpoch||(starts&&!strong.processingActive))return;
        if(strong.midi)strong.midi(status,a,b);
    };
    if(_inputRead)context.audioInputEnabled=^bool{return current()?bool(weak.inputRead()):false;};
    if(_inputWrite)context.setAudioInputEnabled=^(bool enabled){if(current())weak.inputWrite(enabled);};
    [self configureTaskContext:&context forNode:targetID];
        if(_midiCapture&&tri::nodeHas(descriptor->capabilities,tri::CapMidiCapture)) {
            context.recordingMidi=_midiCaptureToken&&_midiCaptureNode==targetID;
            context.midiCapture=^NSDictionary*(NSString *action,int tempoParameter) {
                SNInstanceEditor *strong=weak;
                if(!current()||strong.selected!=targetID)return @{};
                if([action isEqual:@"start"]) {
                    if(strong->_midiCaptureToken)return @{@"error":@"请先完成其他 MIDI 录制。"};
                    NSDictionary *reply=strong->_midiCapture(@{@"action":action,@"node":@(targetID),
                        @"tempoParameter":@(tempoParameter)});
                    strong->_midiCaptureToken=[reply[@"token"] unsignedLongLongValue];
                    if(strong->_midiCaptureToken)strong->_midiCaptureNode=targetID;
                    return reply;
                }
                if(strong->_midiCaptureNode!=targetID||!strong->_midiCaptureToken)return @{};
                NSDictionary *reply=strong->_midiCapture(@{@"action":action,
                    @"token":@(strong->_midiCaptureToken)});
                strong->_midiCaptureToken=0;strong->_midiCaptureNode=0;
                return reply;
            };
        }

    return context;
}
-(void)configureTaskContext:(tri::NodeEditorContext *)context forNode:(uint64_t)uid {
    NSMutableDictionary *node=[self node:uid];
    if(!node||!tri::nodeHas(tri::nodeCapabilities(typeOf(node)),tri::CapTask))return;
    __weak SNInstanceEditor *weak=self;
    NSDictionary *targetDocument=_document,*targetNode=node;
    NSDictionary *targetInputs=copyJSON(taskInputSnapshot(node));
    auto catalog=tri::nodeCatalogSnapshot();
    context->captureTaskContext=[weak,targetDocument,targetNode,uid,catalog](tri::NodeEditorContext& current) {
        if(!NSThread.isMainThread)return false;
        SNInstanceEditor *strong=weak;
        auto ownsSelection=[&] {
            return strong&&tri::nodeCatalogSnapshot()==catalog&&strong.document==targetDocument&&strong.selected==uid&&[strong node:uid]==targetNode;
        };
        if(!ownsSelection())return false;
        // Commit an active inspector field before capturing the request.
        if(strong.window&&![strong.window makeFirstResponder:strong->_canvas])return false;
        if(!ownsSelection())return false;
        current=[strong editorContextForNode:uid];
        return bool(current.submitTask);
    };
    context->submitTask=[weak,targetDocument,targetNode,targetInputs,uid,catalog](soundnet::TaskService::Job job,
                                                           tri::NodeTaskApply apply,std::string& why) {
        if(!NSThread.isMainThread) {why="Node editor tasks must be submitted on the editor thread.";return false;}
        SNInstanceEditor *strong=weak;
        if(!strong||tri::nodeCatalogSnapshot()!=catalog||strong.document!=targetDocument||[strong node:uid]!=targetNode||!apply) {
            why="Node editor is no longer current.";return false;
        }
        if(!strong->taskService)strong->taskService=std::make_unique<soundnet::TaskService>(2,16);
        [strong reconcileBackgroundTasks];
        if(![strong->taskInputs[@(uid)] isEqual:targetInputs]) {
            why="Node editor inputs have changed.";return false;
        }
        soundnet::TaskBinding binding;
        if(!strong->taskService->submit(uid,strong->taskInputRevisions[uid],std::move(job),binding,why))return false;
        strong->taskApplications[binding.id]=[catalog,apply=std::move(apply)](const soundnet::TaskResult& result,NSDictionary* node)->NSDictionary* {
            if(tri::nodeCatalogSnapshot()!=catalog)return nil;
            return apply(result,node);
        };
        return true;
    };
    context->cancelTask=[weak,targetDocument,targetNode,uid,catalog] {
        if(!NSThread.isMainThread)return;
        SNInstanceEditor *strong=weak;
        if(strong&&tri::nodeCatalogSnapshot()==catalog&&strong.document==targetDocument&&[strong node:uid]==targetNode&&strong->taskService)
            strong->taskService->cancel(uid);
    };
    context->taskSnapshot=[weak,targetDocument,targetNode,uid,catalog](soundnet::TaskService::Snapshot& snapshot) {
        if(!NSThread.isMainThread)return false;
        SNInstanceEditor *strong=weak;
        return strong&&tri::nodeCatalogSnapshot()==catalog&&strong.document==targetDocument&&[strong node:uid]==targetNode&&strong->taskService&&
            strong->taskService->snapshot(uid,snapshot);
    };
}
-(void)beginTaskSession {
    ++_playbackEpoch;
    [_canvas clearPlacementFeedback];
    _moving=0;
    if(_wireSource){_wireSource=0;_canvas.needsDisplay=YES;}
    [self cancelMidiCapture];
    if(taskService)taskService->beginSession();
    taskInputRevisions.clear();taskApplications.clear();taskInputs=[NSMutableDictionary new];
}
-(void)cancelMidiCapture {
    if(_midiCaptureToken&&_midiCapture)
        _midiCapture(@{@"action":@"cancel",@"token":@(_midiCaptureToken)});
    _midiCaptureToken=0;_midiCaptureNode=0;
}
-(void)reconcileBackgroundTasks {
    if(!taskService)return;
    if(!taskInputs)taskInputs=[NSMutableDictionary new];
    NSMutableSet<NSNumber *> *alive=[NSMutableSet new];
    std::map<uint64_t,uint64_t> revisions;
    for(NSDictionary *node in _document[@"nodes"]) {
        if(!tri::nodeHas(tri::nodeCapabilities(typeOf(node)),tri::CapTask))continue;
        NSNumber *uid=node[@"id"];if(!uid)continue;
        [alive addObject:uid];
        // Configuration is the immutable input of this generation task. Layout
        // and unrelated graph edits do not invalidate it. Future task adapters
        // include their declared resource/input revisions in their own snapshot.
        NSDictionary *inputs=taskInputSnapshot(node);
        if(![taskInputs[uid] isEqual:inputs]) {
            taskInputs[uid]=copyJSON(inputs);
            taskInputRevisions[uid.unsignedLongLongValue]=++nextTaskInputRevision;
        }
        revisions[uid.unsignedLongLongValue]=taskInputRevisions[uid.unsignedLongLongValue];
    }
    for(NSNumber *uid in taskInputs.allKeys)if(![alive containsObject:uid]) {
        [taskInputs removeObjectForKey:uid];
        taskInputRevisions.erase(uid.unsignedLongLongValue);
    }
    taskService->reconcile(revisions);
    std::set<uint64_t> currentTasks;
    for(const auto& revision:revisions) {
        soundnet::TaskService::Snapshot snapshot;
        if(taskService->snapshot(revision.first,snapshot))currentTasks.insert(snapshot.binding.id);
    }
    for(auto it=taskApplications.begin();it!=taskApplications.end();)
        if(!currentTasks.count(it->first))it=taskApplications.erase(it);else ++it;
}
-(void)drainBackgroundTasks {
    if(_catalogGeneration!=tri::nodeCatalogSnapshot()->generation) {
        TRIRefreshNodeCatalog(self,nullptr);return;
    }
    if(!taskService)return;
    [self reconcileBackgroundTasks];
    bool changed=false;
    for(auto& completion:taskService->takeCompleted()) {
        const uint64_t uid=completion.binding.node;
        NSMutableDictionary *node=[self node:uid];
        if(!node)continue;
        auto application=taskApplications.find(completion.binding.id);
        if(application!=taskApplications.end()) {
            auto apply=std::move(application->second);taskApplications.erase(application);
            NSDictionary *replacement=apply(completion.result,copyJSON(node));
            if([replacement isKindOfClass:NSDictionary.class]) {
                NSDictionary *previous=copyJSON(_document);
                applyNodeContent(node,replacement);
                if(![self commit:previous]) {changed=true;continue;}
            }
            if(!completion.result.message.empty()&&uid==_selected)
                [self message:[NSString stringWithUTF8String:completion.result.message.c_str()]];
            changed=true;continue;
        }
    }
    if(changed)[self inspect];
}
// Generation for CapTask nodes is submitted from each node's own panel
// through configureTaskContext; the central editor only drains results.
// The score loop's return half: ABC becomes an ordinary MIDI clip, so the
// piano roll, the scale quantiser, the arranger and MIDI export all apply.
-(void)textChanged:(NSTextField *)field {
    NSMutableDictionary *node=[self node:_selected];
    if(!node)return;
    const tri::TextSpec *spec=tri::nodeText(typeOf(node),size_t(field.tag));
    if(!spec||spec->readOnly)return;
    NSMutableDictionary *texts=[node[@"texts"] isKindOfClass:NSDictionary.class]
        ? [node[@"texts"] mutableCopy] : [NSMutableDictionary new];
    NSString *key=[NSString stringWithUTF8String:spec->id];
    NSString *value=field.stringValue ?: @"";
    if([(texts[key] ?: @"") isEqualToString:value])return;
    const NSUInteger limit=spec->maxBytes;
    if([value lengthOfBytesUsingEncoding:NSUTF8StringEncoding]>limit) {[self message:SNLocalized(@"文本过长。")];return;}
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
    const auto* descriptor=tri::nodeSpec(type);
    if(!descriptor) {
        [self label:[NSString stringWithFormat:@"%@ · v%@ / state %@",node[@"nodeKey"] ?: @"?",
            node[@"nodeVersion"] ?: @1,node[@"stateVersion"] ?: @1] y:y];y+=70;
    }
    if(descriptor)if(auto factory=tri::nodeEditorFactory(descriptor->key)) {
        tri::NodeEditorContext context=[self editorContextForNode:_selected];
        NSView* panel=factory(context);
        if(panel) {panel.frame=NSMakeRect(14,y,270,NSHeight(panel.frame));[_inspector addSubview:panel];y+=NSHeight(panel.frame)+12;}
    }
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
        for(const auto& preset:presets)[popup addItemWithTitle:SNNodeLocalized(type,[NSString stringWithUTF8String:preset.name])];
        popup.target=self;popup.action=@selector(applyPreset:);
        [_inspector addSubview:popup];y+=38;
    }
    if(!controls(type).empty()) {
        [self button:SNLocalized(@"恢复默认参数") frame:NSMakeRect(14,y,270,28) action:@selector(resetParams:) parent:_inspector];y+=38;
    }
    if(tri::nodeTextCount(type)>0) {
        for(int i=0;i<tri::nodeTextCount(type);++i) {
            const tri::TextSpec *spec=tri::nodeText(type,size_t(i));
            if(spec->hidden)continue;
            NSString *caption=SNNodeLocalized(type,[NSString stringWithUTF8String:spec->label]);
            NSTextField *label=[NSTextField labelWithString:caption];
            label.font=[NSFont systemFontOfSize:12];label.frame=NSMakeRect(14,y,270,18);
            [_inspector addSubview:label];y+=20;
            NSString *key=[NSString stringWithUTF8String:spec->id];
            NSString *value=node[@"texts"][key] ?: [NSString stringWithUTF8String:spec->defaultValue];
            const bool multiline=spec->multiline;
            NSTextField *field=[[NSTextField alloc]initWithFrame:NSMakeRect(14,y,270,multiline?66:25)];
            field.stringValue=value;field.editable=!spec->readOnly;field.tag=i;field.target=self;field.action=@selector(textChanged:);
            if(descriptor&&tri::nodeHas(descriptor->capabilities,tri::CapTask))
                field.cell.sendsActionOnEndEditing=YES;
            field.font=[NSFont systemFontOfSize:12];
            field.placeholderString=caption;
            if(multiline) {field.cell.wraps=YES;field.usesSingleLineMode=NO;}
            field.accessibilityLabel=caption;[_inspector addSubview:field];
            y+=multiline?74:33;
        }
    }
    if(!descriptor||!descriptor->customParameterEditor) {
        std::vector<tri::ParamGroup> groups;
        for(int p:controls(type)) {
            if(tri::paramSpec(p)->hidden)continue; // A trigger is an action, not an editable counter.
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
                if(tri::paramSpec(p)->hidden || tri::paramSpec(p)->group!=group)continue;
                const auto& info=tri::graphParamInfo(p);id val=paramValue(node,p);
                NSString *caption=parameterName(type,p);
                if(info.unit && *info.unit)caption=[caption stringByAppendingFormat:@" · %s",info.unit];
                NSTextField *label=[NSTextField labelWithString:caption];
                label.frame=NSMakeRect(16,y+3,184,20);label.font=[NSFont systemFontOfSize:12];
                label.toolTip=caption;[_inspector addSubview:label];
                NSArray<NSString*> *options=paramOptions(type,p);
                if(options) {
                    label.frame=NSMakeRect(16,y,268,20);
                    NSPopUpButton *popup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(14,y+22,270,27) pullsDown:NO];
                    [popup addItemsWithTitles:options];
                    NSInteger index=(NSInteger)std::lround([val doubleValue]-info.min);
                    [popup selectItemAtIndex:std::clamp<NSInteger>(index,0,(NSInteger)options.count-1)];
                    popup.tag=p;popup.target=self;popup.action=@selector(choiceChanged:);
                    [popup setAccessibilityLabel:parameterName(type,p)];[_inspector addSubview:popup];y+=61;continue;
                }
                NSTextField *field=[[NSTextField alloc]initWithFrame:NSMakeRect(206,y,78,25)];
                field.stringValue=[NSString stringWithFormat:@"%.4g",[val doubleValue]];
                field.alignment=NSTextAlignmentRight;field.font=[NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular];
                field.tag=p;field.target=self;field.action=@selector(changed:);
                field.accessibilityLabel=parameterName(type,p);[_inspector addSubview:field];
                if(info.max-info.min<=20000) {
                    NSSlider *slider=[NSSlider sliderWithValue:[val doubleValue] minValue:info.min maxValue:info.max target:self action:@selector(sliderChanged:)];
                    slider.frame=NSMakeRect(14,y+28,270,18);slider.tag=NSInteger(p)+100000;slider.continuous=NO;
                    slider.accessibilityLabel=parameterName(type,p);[_inspector addSubview:slider];y+=60;
                } else y+=39;
            }
            y+=10;
        }
    }
    if(descriptor&&descriptor->triggerParameter>=0){
        [self button:SNLocalized(@"发送触发信号") frame:NSMakeRect(14,y,270,30) action:@selector(trigger:) parent:_inspector];y+=44;
    }
    if(descriptor&&descriptor->assetKind) {
        NSString *kind=@(descriptor->assetKind);
        NSString *caption=[kind isEqual:@"audio"]?SNLocalized(@"载入音频…"):[kind isEqual:@"clip"]?SNLocalized(@"载入 MIDI Clip…"):SNLocalized(@"载入音符模型…");
        [self button:caption frame:NSMakeRect(14,y,270,30) action:@selector(import:) parent:_inspector];y+=42;
        NSDictionary *asset=node[@"asset"];
        [self label:asset[@"name"] ?: SNLocalized(@"未载入素材") y:y];y+=28;
        if([kind isEqual:@"audio"]&&asset[@"pcm"]) {
            double rate=[asset[@"rate"] doubleValue];int channels=[asset[@"channels"] intValue];
            NSData *pcm=[[NSData alloc]initWithBase64EncodedString:asset[@"pcm"] options:0];
            double seconds=rate>0?double(pcm.length)/4.0/std::max(1,channels)/rate:0;
            [self label:[NSString stringWithFormat:SNLocalized(@"%.2f 秒 · %@ · %.0f Hz"),seconds,channels==1?SNLocalized(@"单声道"):SNLocalized(@"立体声"),rate] y:y];y+=26;
            SNWaveformView *wave=[[SNWaveformView alloc]initWithFrame:NSMakeRect(14,y,270,58)];wave.pcm=pcm;wave.channels=channels;[_inspector addSubview:wave];y+=66;
        }
    }
    if(descriptor&&descriptor->help){[self label:SNNodeLocalized(type,@(descriptor->help)) y:y];y+=80;}
    _inspector.frame=NSMakeRect(0,0,298,MAX(_inspectorScroll.contentSize.height,y+20));
    [_inspector scrollPoint:sameNode?NSMakePoint(0,MIN(scrollOrigin.y,MAX(0,NSHeight(_inspector.frame)-_inspectorScroll.contentSize.height))):NSZeroPoint];
}
-(void)runExport:(NSInteger)kind toURL:(NSURL *)url seconds:(double)seconds rate:(double)rate bits:(int)bits {
    if(![self beginExport])return;
    auto cancelled=_exportCancellation;
    auto catalog=tri::nodeCatalogSnapshot();
    NSDictionary *document=copyJSON(_document);
    [self message:SNLocalized(@"正在渲染，可点击取消处理或停止。")];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        @autoreleasepool {
            tri::NodeCatalogReadScope scope(catalog);
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
                if(cancelled->load()||tri::nodeCatalogSnapshot()!=catalog)message=SNLocalized(@"已取消处理，未写入导出文件。");
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
    tri::GraphSpec projectGraph;tri::ProjectEnvironment environment;
    if(triassets::decodeGraph(_document,projectGraph,nullptr,false,true))tri::projectEnvironment(projectGraph,environment);
    for(NSNumber* value in @[@44100,@48000,@96000]) {
        [ratePopup addItemWithTitle:[NSString stringWithFormat:@"%g kHz",value.doubleValue/1000.0]];
        ratePopup.lastItem.representedObject=value;
    }
    BOOL foundRate=NO;
    for(NSMenuItem* item in ratePopup.itemArray)if([item.representedObject doubleValue]==environment.sampleRate) {
        [ratePopup selectItem:item];foundRate=YES;break;
    }
    if(!foundRate) {[ratePopup addItemWithTitle:[NSString stringWithFormat:@"%g Hz",environment.sampleRate]];
        ratePopup.lastItem.representedObject=@(environment.sampleRate);[ratePopup selectItem:ratePopup.lastItem];}
    [accessory addSubview:ratePopup];
    NSTextField *bitsLabel=[NSTextField labelWithString:SNLocalized(@"位深")];
    bitsLabel.frame=NSMakeRect(180,30,40,20);[accessory addSubview:bitsLabel];
    NSPopUpButton *bitsPopup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(222,26,70,26)];
    [bitsPopup addItemsWithTitles:@[@"16-bit",@"24-bit",@"32-bit float"]];[bitsPopup selectItemAtIndex:environment.exportBits==16?0:environment.exportBits==24?1:2];[accessory addSubview:bitsPopup];
    panel.accessoryView=accessory;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result!=NSModalResponseOK)return;
        double seconds=std::max(1.0,std::min(120.0,secondsField.doubleValue));
        double rate=[ratePopup.selectedItem.representedObject doubleValue];
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
    tri::GraphSpec projectGraph;tri::ProjectEnvironment environment;
    if(triassets::decodeGraph(_document,projectGraph,nullptr,false,true))tri::projectEnvironment(projectGraph,environment);
    for(NSNumber* value in @[@44100,@48000,@96000]) {
        [ratePopup addItemWithTitle:[NSString stringWithFormat:@"%g kHz",value.doubleValue/1000.0]];
        ratePopup.lastItem.representedObject=value;
    }
    BOOL foundRate=NO;
    for(NSMenuItem* item in ratePopup.itemArray)if([item.representedObject doubleValue]==environment.sampleRate) {
        [ratePopup selectItem:item];foundRate=YES;break;
    }
    if(!foundRate) {[ratePopup addItemWithTitle:[NSString stringWithFormat:@"%g Hz",environment.sampleRate]];
        ratePopup.lastItem.representedObject=@(environment.sampleRate);[ratePopup selectItem:ratePopup.lastItem];}
    [accessory addSubview:ratePopup];
    NSTextField *bitsLabel=[NSTextField labelWithString:SNLocalized(@"位深")];
    bitsLabel.frame=NSMakeRect(180,30,40,20);[accessory addSubview:bitsLabel];
    NSPopUpButton *bitsPopup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(222,26,70,26)];
    [bitsPopup addItemsWithTitles:@[@"16-bit",@"24-bit",@"32-bit float"]];[bitsPopup selectItemAtIndex:environment.exportBits==16?0:environment.exportBits==24?1:2];[accessory addSubview:bitsPopup];
    panel.accessoryView=accessory;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result!=NSModalResponseOK)return;
        double seconds=std::max(1.0,std::min(120.0,secondsField.doubleValue));
        double rate=[ratePopup.selectedItem.representedObject doubleValue];
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
    [self message:[NSString stringWithFormat:SNLocalized(@"已应用预设：%@"),SNNodeLocalized(typeOf(node),@(presets[index].name))]];
}
-(void)resetParams:(id)sender {
    NSMutableDictionary *node=[self node:_selected];if(!node)return;
    NSDictionary *previous=copyJSON(_document);
    for(int p:controls(typeOf(node)))
        setParam(node,p,tri::graphParamInfo(p).def);
    [self commit:previous];[self inspect];[self message:SNLocalized(@"已恢复默认参数。")];
}
-(void)save:(id)sender {
    NSSavePanel *panel=[NSSavePanel savePanel];panel.nameFieldStringValue=_projectURL.lastPathComponent ?: @"Soundnet.json";
    NSString *folder=projectFolder(_document);if(folder.length)panel.directoryURL=[NSURL fileURLWithPath:folder isDirectory:YES];
    else if(_projectURL)panel.directoryURL=[_projectURL URLByDeletingLastPathComponent];
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result!=NSModalResponseOK)return;
        NSDictionary *targetDocument=self.document;
        NSMutableDictionary *snapshot=copyJSON(self.document);NSURL *url=panel.URL;
        setProjectFolder(snapshot,url.URLByDeletingLastPathComponent.path);
        if(!self->_autosaveQueue)self->_autosaveQueue=dispatch_queue_create("com.soundnet.autosave",DISPATCH_QUEUE_SERIAL);
        dispatch_async(self->_autosaveQueue,^{
            NSString *why=nil;const bool saved=triassets::writeProject(snapshot,url,&why);
            dispatch_async(dispatch_get_main_queue(),^{
                if(saved&&self.document==targetDocument) {
                    self.projectURL=url;setProjectFolder(self.document,url.URLByDeletingLastPathComponent.path);
                    [self commit:nil];[self inspect];
                }
                [self message:saved?SNLocalized(@"已保存工程及素材。"):why];
            });
        });
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
    else { [self message:SNLocalized(@"不支持此工程格式版本。")];return NO; }
    tri::GraphSpec graph;
    if(!document || !triassets::decodeGraph(document,graph,&why,true,true)) {
        [self message:why ?: SNLocalized(@"无效的工作流。")];return NO;
    }
    NSDictionary *previous=copyJSON(self.document);
    [self beginTaskSession];
    self.document=copyJSON(document);self.projectURL=nil;
    [self.selection removeAllObjects];self.selected=0;self.selectedEdge=-1;
    BOOL applied=[self commit:previous];[self fit:nil];[self inspect];
    return applied;
}
-(BOOL)openProject:(NSURL *)url {
    NSString *why=nil;
    NSDictionary *file=triassets::readProject(url,&why);
    if(!file){[self message:why];return NO;}
    NSMutableDictionary *document=copyJSON(file);
    setProjectFolder(document,url.URLByDeletingLastPathComponent.path);
    if(![self adoptFile:document])return NO;
    self.projectURL=url;
    return YES;
}
-(void)open:(id)sender {
    NSOpenPanel *panel=[NSOpenPanel openPanel];panel.allowedContentTypes=@[UTTypeJSON];
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result==NSModalResponseOK)[self openProject:panel.URL];
    }];
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
    const auto *descriptor=tri::nodeSpec(type);
    if(!descriptor || !descriptor->instantiable)return 0;
    if(descriptor->environmentRole!=tri::EnvironmentRole::None) {
        for(NSDictionary *existing in _document[@"nodes"]) {
            const auto *other=tri::nodeSpec(typeOf(existing));
            if(other && other->environmentRole==descriptor->environmentRole) {
                const uint64_t uid=[existing[@"id"] unsignedLongLongValue];
                [self selectOnly:uid];return uid;
            }
        }
    }
    NSDictionary *previous=copyJSON(_document);
    uint64_t next=1;
    for(NSDictionary *node in _document[@"nodes"])next=std::max(next,[node[@"id"] unsignedLongLongValue]+1);
    NSMutableDictionary *starts=[NSMutableDictionary new];
    if(![self makeNode:type at:point id:next expand:(NSEvent.modifierFlags & NSEventModifierFlagShift)!=0 starts:starts])return 0;
    _selected=next;
    [_selection removeAllObjects];[_selection addObject:@(next)];
    const BOOL applied=[self commit:previous];
    if(applied)[_canvas animatePlacements:starts focus:next];
    [self inspect];
    return applied?next:0;
}
-(void)insertFromLibrary:(int)type {
    NSSize size=_canvas.bounds.size;
    NSPoint center=[self world:NSMakePoint(size.width/2-86,size.height/2-50)];
    if([self insertNodeType:type atPoint:center])
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
    double availableW=std::max(200.,size.width-48),availableH=std::max(160.,size.height-48-zoneHeaderHeight);
    _zoom=std::clamp(std::min(availableW/all.size.width,availableH/all.size.height),.1,1.2);
    _origin=NSMakePoint((size.width-all.size.width*_zoom)/2-all.origin.x*_zoom,
                        (size.height+zoneHeaderHeight-all.size.height*_zoom)/2-all.origin.y*_zoom);
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
    NSMutableDictionary<NSNumber*,NSNumber*> *nextY=[NSMutableDictionary new];
    NSArray<NSDictionary*> *ordered=[nodes sortedArrayUsingComparator:^NSComparisonResult(NSDictionary *a,NSDictionary *b){
        return [a[@"id"] compare:b[@"id"]];
    }];
    NSDictionary *previous=copyJSON(_document);
    for(NSDictionary *node in ordered) {
        NSNumber *level=depth[node[@"id"]] ?: @0;
        double y=nextY[level] ? nextY[level].doubleValue : 60;
        nextY[level]=@(y+NSHeight(nodeRect(node))+30);
        NSMutableDictionary *mutableNode=(NSMutableDictionary *)node;
        mutableNode[@"x"]=@(80+[level doubleValue]*240);
        mutableNode[@"y"]=@(y);
    }
    [self commit:previous];[self fit:nil];_canvas.needsDisplay=YES;
    [self message:[NSString stringWithFormat:SNLocalized(@"已按信号深度排列 %lu 个节点"),(unsigned long)nodes.count]];
}
-(void)blank:(id)sender {
    NSDictionary *previous=copyJSON(_document);
    [self beginTaskSession];
    _document=tri::newProjectDocument();
    _selected=0;[self commit:previous];[self inspect];
}
-(void)tick {
    if(_catalogGeneration!=tri::nodeCatalogSnapshot()->generation) {
        TRIRefreshNodeCatalog(self,nullptr);return;
    }
    [self updateProcessingControls];
    NSDictionary *remote=_assetsRead ? _assetsRead()[@"graph"] : nil;
    if(remote && ![remote isEqual:_document] && !_moving && !_wireSource) {
        NSString *reason=nil;
        NSDictionary *canonical=triassets::canonicalGraph(remote,&reason);
        if(!canonical)return;
        remote=canonical;
        if([remote isEqual:_document])return;
        if(![remote[@"projectId"] isEqual:_document[@"projectId"]])[self beginTaskSession];
        _document=copyJSON(remote);[self reconcileBackgroundTasks];[self pruneSelection];[self inspect];_canvas.needsDisplay=YES;
    }
    [self drainBackgroundTasks];
}
@end

namespace {
int nodePortRows(int type) {
    const auto *spec=tri::nodeSpec(type);
    return spec?int(std::max(spec->inputs.size(),spec->outputs.size())):0;
}
bool canvasParameter(const tri::NodeSpec& node,int parameter) {
    const auto *spec=tri::paramSpec(parameter);
    return spec && !spec->hidden && parameter!=node.triggerParameter;
}
bool hasAudioPreview(const tri::NodeSpec& node) {
    return node.assetKind && std::strcmp(node.assetKind,"audio")==0;
}
double cardDetailsTop(int type) {
    return std::max(40,38+nodePortRows(type)*24);
}
NSSize cardSize(int type) {
    double bottom=cardDetailsTop(type)+8;
    if(const auto *spec=tri::nodeSpec(type)) {
        int count=0;
        for(int p:spec->params)if(canvasParameter(*spec,p) && ++count==2)break;
        bottom+=count*16;
        if(spec->environmentRole==tri::EnvironmentRole::Timecode)bottom+=16;
        if(hasAudioPreview(*spec))bottom+=35;
    }
    return NSMakeSize(cardWidth,std::max(88.0,bottom));
}
NSRect nodeRect(NSDictionary *node) {
    return NSRect{NSMakePoint([node[@"x"] doubleValue],[node[@"y"] doubleValue]),cardSize(typeOf(node))};
}
NSPoint jack(NSRect r,bool out,int port) {
    return NSMakePoint(out?NSMaxX(r):NSMinX(r),NSMinY(r)+47+24*port);
}
double curveX(NSPoint a,NSPoint c1,NSPoint c2,NSPoint b,double t) {
    double u=1-t;return u*u*u*a.x+3*u*u*t*c1.x+3*u*t*t*c2.x+t*t*t*b.x;
}
double curveY(NSPoint a,NSPoint c1,NSPoint c2,NSPoint b,double t) {
    double u=1-t;return u*u*u*a.y+3*u*u*t*c1.y+3*u*t*t*c2.y+t*t*t*b.y;
}
bool nearEdge(NSPoint from,NSPoint to,NSPoint point,double tolerance) {
    NSPoint c1=NSMakePoint(from.x+70,from.y),c2=NSMakePoint(to.x-70,to.y);
    for(int i=0;i<=32;++i) {
        double t=i/32.0;
        if(std::hypot(curveX(from,c1,c2,to,t)-point.x,curveY(from,c1,c2,to,t)-point.y)<tolerance)return true;
    }
    return false;
}
void drawText(NSString *text,NSRect rect,NSColor *color,double size=12,
              NSTextAlignment alignment=NSTextAlignmentLeft) {
    NSMutableParagraphStyle *style=[NSMutableParagraphStyle new];
    style.alignment=alignment;style.lineBreakMode=NSLineBreakByTruncatingTail;
    [text drawInRect:rect withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:size],
        NSForegroundColorAttributeName:color,NSParagraphStyleAttributeName:style}];
}
NSString *canvasParameterValue(int type,int p,NSDictionary *node) {
    const auto *spec=tri::paramSpec(p);
    const auto& info=tri::graphParamInfo(p);
    const double value=paramValue(node,p).doubleValue;
    if(spec->options && spec->optionCount) {
        const auto index=std::clamp<long>(std::lround(value-info.min),0,spec->optionCount-1);
        return SNNodeLocalized(type,@(spec->options[index]));
    }
    NSString *number=spec->discrete?[NSString stringWithFormat:@"%.0f",value]:
        [NSString stringWithFormat:@"%.4g",value];
    return info.unit && *info.unit ? [number stringByAppendingFormat:@" %s",info.unit] : number;
}
constexpr NSUInteger waveformColumns=84;
NSData *waveformEnvelope(NSString *encoded,NSUInteger channels) {
    NSData *pcm=[[NSData alloc]initWithBase64EncodedString:encoded options:0];
    const NSUInteger frames=pcm.length/(sizeof(float)*channels);
    if(!frames)return [NSData data];
    const float *samples=static_cast<const float *>(pcm.bytes);
    float envelope[waveformColumns][2]={};
    for(NSUInteger x=0;x<waveformColumns;++x) {
        const NSUInteger start=frames*x/waveformColumns;
        const NSUInteger end=std::max(start+1,frames*(x+1)/waveformColumns);
        const NSUInteger step=std::max<NSUInteger>(1,(end-start)/512);
        for(NSUInteger frame=start;frame<end;frame+=step) {
            for(NSUInteger c=0;c<channels;++c) {
                const float value=samples[frame*channels+c];
                if(!std::isfinite(value))continue;
                envelope[x][0]=std::min(envelope[x][0],std::max(-1.f,value));
                envelope[x][1]=std::max(envelope[x][1],std::min(1.f,value));
            }
        }
    }
    return [NSData dataWithBytes:envelope length:sizeof(envelope)];
}
void drawCable(NSPoint from,NSPoint to,NSColor *color,CGFloat width) {
    NSBezierPath *path=[NSBezierPath new];[path moveToPoint:from];
    [path curveToPoint:to controlPoint1:NSMakePoint(from.x+70,from.y) controlPoint2:NSMakePoint(to.x-70,to.y)];
    [color setStroke];path.lineWidth=width;[path stroke];
}

using soundnet::canvas::Extents;
using soundnet::canvas::ZoneShifts;
using soundnet::canvas::ZoneInsertion;

NSRect clearVerticalSlot(NSRect proposed,NSArray<NSDictionary *> *nodes,
                         const ZoneShifts& shifts,uint64_t moving=0) {
    // Only the placed card may choose a nearby clear y.
    constexpr double clearance=12;
    std::vector<std::pair<double,double>> blocked;
    for(NSDictionary *node in nodes) {
        if(moving && [node[@"id"] unsignedLongLongValue]==moving)continue;
        NSRect occupied=NSInsetRect(nodeRect(node),-clearance,-clearance);
        const Stage stage=canvasStage(typeOf(node));
        if(stage!=Stage::Free)occupied.origin.x+=shifts[int(stage)];
        if(NSMaxX(occupied)>NSMinX(proposed) && NSMinX(occupied)<NSMaxX(proposed))
            blocked.emplace_back(NSMinY(occupied)-NSHeight(proposed),NSMaxY(occupied));
    }
    std::sort(blocked.begin(),blocked.end());
    for(size_t i=0;i<blocked.size();) {
        auto [top,bottom]=blocked[i++];
        while(i<blocked.size() && blocked[i].first<=bottom)
            bottom=std::max(bottom,blocked[i++].second);
        const double y=NSMinY(proposed);
        if(y>top && y<bottom) {
            proposed.origin.y=y-top<=bottom-y?top:bottom;
            break;
        }
        if(y<=top)break;
    }
    return proposed;
}

NSRect placementRect(const Zones& zones,Stage stage,NSRect proposed,
                     NSArray<NSDictionary *> *nodes,uint64_t moving) {
    const double x=zones.placementX(stage,NSMidX(proposed));
    if(x==NSMidX(proposed))return proposed;
    proposed.origin.x=x-NSWidth(proposed)/2;
    return clearVerticalSlot(proposed,nodes,{},moving);
}

struct RenderNode {
    NSDictionary *node;
    NSRect rect;
    int type;
    Stage stage;
};
struct RenderCable {
    size_t source,target;
    int output,input;
    tri::Signal signal;
    NSInteger index;
};

NSRect groupRect(NSDictionary *group) {
    return NSMakeRect([group[@"x"] doubleValue],[group[@"y"] doubleValue],
                      [group[@"w"] doubleValue],[group[@"h"] doubleValue]);
}

double zoneShift(const ZoneShifts& shifts,Stage stage) {
    return stage==Stage::Free?0:shifts[int(stage)];
}

NSRect shiftedGroupRect(NSRect bounds,const std::vector<RenderNode>& nodes,const ZoneShifts& shifts) {
    // Group membership is geometric. Keep the frame around its original members.
    double left=0,right=0;
    bool hasMember=false;
    for(const auto& entry:nodes) {
        if(!NSPointInRect(NSMakePoint(NSMidX(entry.rect),NSMidY(entry.rect)),bounds))continue;
        const double dx=zoneShift(shifts,entry.stage);
        left=hasMember?std::min(left,dx):dx;
        right=hasMember?std::max(right,dx):dx;
        hasMember=true;
    }
    bounds.origin.x+=left;
    bounds.size.width+=right-left;
    return bounds;
}

struct InsertionPlan {
    NSRect card;
    ZoneInsertion layout;
    std::optional<Zones> preview;
};

InsertionPlan planInsertion(const Extents& extents,const Zones& zones,Stage stage,NSRect requested,
                            NSArray<NSDictionary *> *nodes,bool expand) {
    constexpr double margin=20;
    const auto layout=soundnet::canvas::planZoneInsertion(extents,stage,NSMidX(requested),cardWidth+2*margin,
        expand?std::numeric_limits<double>::infinity():soundnet::canvas::automaticZoneShift);
    NSRect target=requested;
    target.origin.x=layout.center-cardWidth/2;
    if(!layout.fits)return {target,layout,{}};
    target=clearVerticalSlot(target,nodes,layout.shifts);
    if(stage==Stage::Free)return {target,layout,{}};

    const int index=int(stage);
    const auto& band=zones.bands[index];
    bool boundedLeft=false,boundedRight=false,shifted=false;
    for(int i=0;i<stageCount;++i) {
        boundedLeft|=i<index && zones.bands[i].occupied;
        boundedRight|=i>index && zones.bands[i].occupied;
        shifted|=layout.shifts[i]!=0;
    }
    const bool grows=!band.occupied || (boundedLeft && NSMinX(target)-margin<band.left) ||
                     (boundedRight && NSMaxX(target)+margin>band.right);
    if(!grows && !shifted)return {target,layout,{}};

    Extents projected=extents;
    for(int i=0;i<stageCount;++i)projected[i].translate(layout.shifts[i]);
    projected[index].add(layout.center);
    return {target,layout,soundnet::canvas::makeZones(projected)};
}

NSColor *stageAccent(int stage) {
    switch(stage) {
        case 0:return [NSColor colorWithSRGBRed:.91 green:.68 blue:.35 alpha:1];
        case 1:return [NSColor colorWithSRGBRed:.64 green:.57 blue:.88 alpha:1];
        case 2:return [NSColor colorWithSRGBRed:.33 green:.76 blue:.74 alpha:1];
        default:return [NSColor colorWithSRGBRed:.56 green:.75 blue:.63 alpha:1];
    }
}

NSString *stageTitle(int stage) {
    return SNLocalized((@[@"音符源区",@"音源区",@"效果区",@"输出区"])[stage]);
}
}
@implementation SNInstanceCanvas {
    NSMapTable<NSDictionary *,NSData *> *_waveforms;
    BOOL _waveformPending;
    __weak NSDictionary *_layoutDocument;
    uint64_t _layoutGeneration;
    Extents _extents;
    Zones _zones;
    std::vector<RenderNode> _renderNodes;
    std::vector<RenderCable> _renderCables;
    std::vector<NSRect> _frameRects;
    CGGradientRef _zoneBlends[stageCount][stageCount];
    std::optional<Zones> _dragZones, _previewZones;
    std::optional<ZoneShifts> _dragPreviewShifts;
    BOOL _placementBlocked, _droppingLibraryNode;
    NSRect _placementPreview;
    Stage _previewStage;
    NSDictionary<NSNumber *,NSValue *> *_placementStarts;
    __weak NSDictionary *_placementDocument;
    CFTimeInterval _placementTime;
    NSTimer *_placementTimer;
    NSPoint _nodeGrab;
    NSDictionary<NSNumber *,NSValue *> *_dragOrigins;
    NSMutableDictionary<NSNumber *,NSValue *> *_dragPositions;
    NSPoint _cameraStart, _cameraEnd, _cameraLast;
}
-(instancetype)initWithFrame:(NSRect)frame {
    if((self=[super initWithFrame:frame]))
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL,SNNodeTypePasteboard]];
    return self;
}
-(void)dealloc {
    for(const auto& row:_zoneBlends)for(CGGradientRef blend:row)if(blend)CGGradientRelease(blend);
}
-(BOOL)isFlipped{return YES;}
-(void)invalidateLayout {
    _layoutDocument=nil;
    if(_dragPreviewShifts)[self clearPlacementFeedback];
}
-(void)refreshLayout {
    NSDictionary *document=_editor.document;
    const auto catalog=tri::nodeCatalogSnapshot();
    if(_layoutDocument==document && _layoutGeneration==catalog->generation)return;
    if(_dragPreviewShifts)[self clearPlacementFeedback];
    _layoutDocument=document;
    _layoutGeneration=catalog->generation;
    _extents={};
    _renderNodes.clear();
    _renderCables.clear();
    NSMutableDictionary<NSNumber *,NSNumber *> *indices=[NSMutableDictionary new];
    for(NSDictionary *node in document[@"nodes"]) {
        const int type=typeOf(node);
        const NSRect rect=nodeRect(node);
        const Stage stage=canvasStage(type);
        if(stage!=Stage::Free)_extents[int(stage)].add(NSMidX(rect));
        indices[node[@"id"]]=@(_renderNodes.size());
        _renderNodes.push_back({node,rect,type,stage});
    }
    _zones=soundnet::canvas::makeZones(_extents);
    NSInteger index=0;
    for(NSDictionary *edge in document[@"edges"]) {
        NSNumber *source=indices[edge[@"source"]],*target=indices[edge[@"target"]];
        if(source && target) {
            const size_t a=source.unsignedIntegerValue,b=target.unsignedIntegerValue;
            const int out=outputPortOf(_renderNodes[a].node,edge),in=inputPortOf(_renderNodes[b].node,edge);
            _renderCables.push_back({a,b,out,in,tri::outputType(_renderNodes[a].type,out),index});
        }
        ++index;
    }
}
-(BOOL)prepareInsertionOfType:(int)type at:(NSPoint *)point expand:(BOOL)expand
                        starts:(NSMutableDictionary<NSNumber *,NSValue *> *)starts {
    [self refreshLayout];
    NSArray<NSMutableDictionary *> *nodes=_editor.document[@"nodes"];
    const auto plan=planInsertion(_extents,_zones,canvasStage(type),{*point,cardSize(type)},nodes,expand);
    if(!plan.layout.fits)return NO;
    const auto& shifts=plan.layout.shifts;
    const bool movesRegions=shifts!=ZoneShifts{};
    const bool fromPreview=_droppingLibraryNode && _dragPreviewShifts.has_value();
    if(movesRegions)for(NSMutableDictionary *group in [_editor groupList]) {
        const NSRect bounds=groupRect(group);
        const NSRect target=shiftedGroupRect(bounds,_renderNodes,shifts);
        if(target.origin.x!=bounds.origin.x)group[@"x"]=@(target.origin.x);
        if(target.size.width!=bounds.size.width)group[@"w"]=@(target.size.width);
    }
    if(movesRegions || fromPreview)for(const auto& entry:_renderNodes) {
        const double dx=zoneShift(shifts,entry.stage);
        const double shown=fromPreview?zoneShift(*_dragPreviewShifts,entry.stage):0;
        auto *node=(NSMutableDictionary *)entry.node;
        // Continue from the position already shown on hover. A matching drop
        // stays in place while the graph transaction adopts the preview.
        if(dx!=shown && !starts[node[@"id"]])
            starts[node[@"id"]]=[NSValue valueWithPoint:NSMakePoint(entry.rect.origin.x+shown,entry.rect.origin.y)];
        if(dx!=0)node[@"x"]=@(entry.rect.origin.x+dx);
    }
    *point=plan.card.origin;
    return YES;
}
-(void)clearPlacementFeedback {
    if(NSIsEmptyRect(_placementPreview) && !_previewZones && !_dragPreviewShifts && !_placementBlocked)return;
    _placementPreview=NSZeroRect;
    _previewZones.reset();
    _dragPreviewShifts.reset();
    _placementBlocked=NO;
    self.needsDisplay=YES;
}
-(void)showPlacementPreview:(NSRect)target from:(NSRect)source stage:(Stage)stage {
    if(NSEqualRects(target,source)){[self clearPlacementFeedback];return;}
    _placementPreview=target;_previewStage=stage;
    self.needsDisplay=YES;
}
-(void)previewPlacementForType:(int)type at:(NSPoint)point libraryDrag:(BOOL)libraryDrag {
    [self refreshLayout];
    const NSRect source={point,cardSize(type)};
    const Stage stage=canvasStage(type);
    const auto plan=planInsertion(_extents,_zones,stage,source,_editor.document[@"nodes"],
        (NSEvent.modifierFlags & NSEventModifierFlagShift)!=0);
    [self clearPlacementFeedback];
    _previewZones=plan.preview;
    if(libraryDrag && plan.layout.fits && plan.layout.shifts!=ZoneShifts{})
        _dragPreviewShifts=plan.layout.shifts;
    _placementBlocked=!plan.layout.fits;
    if(_previewZones || _placementBlocked || !NSEqualRects(plan.card,source)) {
        _placementPreview=plan.card;_previewStage=stage;
        self.needsDisplay=YES;
    }
}
-(void)animatePlacements:(NSDictionary<NSNumber *,NSValue *> *)starts focus:(uint64_t)focus {
    if(!starts.count)return;
    _cameraStart=_cameraEnd=_cameraLast=_editor.origin;
    if(focus && starts[@(focus)] && self.window) {
        NSNumber *uid=@(focus);
        const NSPoint from=[starts[uid] pointValue];
        const NSPoint fromView=NSMakePoint(_editor.origin.x+from.x*_editor.zoom,_editor.origin.y+from.y*_editor.zoom);
        NSDictionary *node=[_editor node:uid.unsignedLongLongValue];
        if(node && NSPointInRect(fromView,NSInsetRect(self.bounds,-12,-12))) {
            const NSRect target=nodeRect(node);
            const double centerX=_editor.origin.x+NSMidX(target)*_editor.zoom;
            const double centerY=_editor.origin.y+NSMidY(target)*_editor.zoom;
            // Bring an offscreen destination into view without reframing the graph.
            if(centerX<NSMinX(self.bounds) || centerX>NSMaxX(self.bounds)) {
                const double inset=std::min(NSWidth(self.bounds)*.5,NSWidth(target)*_editor.zoom*.5+18);
                _cameraEnd.x+=std::clamp(centerX,NSMinX(self.bounds)+inset,NSMaxX(self.bounds)-inset)-centerX;
            }
            if(centerY<zoneHeaderHeight || centerY>NSMaxY(self.bounds)) {
                const double inset=std::min(std::max(0.,NSHeight(self.bounds)-zoneHeaderHeight)*.5,
                    NSHeight(target)*_editor.zoom*.5+12);
                if(NSHeight(self.bounds)>zoneHeaderHeight)
                    _cameraEnd.y+=std::clamp(centerY,zoneHeaderHeight+inset,NSMaxY(self.bounds)-inset)-centerY;
            }
        }
    }
    [_placementTimer invalidate];
    if(NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion) {
        _editor.origin=_cameraEnd;
        _placementTimer=nil;_placementStarts=nil;
        self.needsDisplay=YES;
        return;
    }
    _placementDocument=_editor.document;
    _placementStarts=[starts copy];
    _placementTime=CACurrentMediaTime();
    __weak SNInstanceCanvas *weak=self;
    _placementTimer=[NSTimer timerWithTimeInterval:1./60 repeats:YES block:^(NSTimer *timer) {
        SNInstanceCanvas *canvas=weak;
        if(!canvas){[timer invalidate];return;}
        canvas.needsDisplay=YES;
        const double elapsed=CACurrentMediaTime()-canvas->_placementTime;
        const BOOL current=canvas->_placementDocument==canvas.editor.document;
        if(current && NSEqualPoints(canvas.editor.origin,canvas->_cameraLast) &&
           !NSEqualPoints(canvas->_cameraStart,canvas->_cameraEnd)) {
            const double progress=std::clamp(elapsed/.24,0.,1.);
            const double t=progress*progress*(3-2*progress);
            canvas->_cameraLast=NSMakePoint(canvas->_cameraStart.x+(canvas->_cameraEnd.x-canvas->_cameraStart.x)*t,
                                           canvas->_cameraStart.y+(canvas->_cameraEnd.y-canvas->_cameraStart.y)*t);
            canvas.editor.origin=canvas->_cameraLast;
        }
        if(elapsed>=.24 || !current) {
            [timer invalidate];
            canvas->_placementTimer=nil;
            canvas->_placementStarts=nil;
        }
    }];
    [NSRunLoop.mainRunLoop addTimer:_placementTimer forMode:NSRunLoopCommonModes];
    self.needsDisplay=YES;
}
-(NSRect)cardRectForNode:(NSDictionary *)node {
    return [self displayRect:nodeRect(node) forNode:node stage:canvasStage(typeOf(node))];
}
-(NSRect)displayRect:(NSRect)rect forNode:(NSDictionary *)node stage:(Stage)stage {
    if(_dragPreviewShifts) {
        rect.origin.x+=zoneShift(*_dragPreviewShifts,stage);
        return rect;
    }
    if(_editor.moving) {
        NSValue *dragged=_dragPositions[node[@"id"]];
        if(dragged) {
            rect.origin=dragged.pointValue;
            return rect;
        }
    }
    if(!_placementStarts.count || _placementDocument!=_editor.document)return rect;
    NSValue *start=_placementStarts[node[@"id"]];
    if(!start || [node[@"id"] unsignedLongLongValue]==_editor.moving)return rect;
    const double progress=std::clamp((CACurrentMediaTime()-_placementTime)/.24,0.,1.);
    const double t=progress*progress*(3-2*progress);
    rect.origin=NSMakePoint(start.pointValue.x+(rect.origin.x-start.pointValue.x)*t,
                            start.pointValue.y+(rect.origin.y-start.pointValue.y)*t);
    return rect;
}
-(NSArray<NSURL *> *)draggedURLs:(id<NSDraggingInfo>)sender {
    return [sender.draggingPasteboard readObjectsForClasses:@[NSURL.class]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
}
-(NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    _cameraEnd=_cameraStart;
    const NSPoint view=[self convertPoint:sender.draggingLocation fromView:nil];
    NSPoint at=[_editor world:view];
    NSString *nodeType=[sender.draggingPasteboard stringForType:SNNodeTypePasteboard];
    if(nodeType && tri::nodeMeta(nodeType.intValue)) {
        [self previewPlacementForType:nodeType.intValue at:NSMakePoint(at.x-cardWidth/2,at.y-40) libraryDrag:YES];
        return NSDragOperationCopy;
    }
    const BOOL clipMode=([NSEvent modifierFlags]&NSEventModifierFlagOption)!=0;
    for(NSURL *url in [self draggedURLs:sender]) {
        if(const auto *spec=fileDropSpec(url,clipMode)) {
            [self previewPlacementForType:spec->type at:at libraryDrag:NO];
            return NSDragOperationCopy;
        }
    }
    [self clearPlacementFeedback];
    return NSDragOperationNone;
}
-(NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender { return [self draggingEntered:sender]; }
-(void)draggingExited:(id<NSDraggingInfo>)sender {[self clearPlacementFeedback];}
-(void)draggingEnded:(id<NSDraggingInfo>)sender {[self clearPlacementFeedback];}
-(BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSString *nodeType=[sender.draggingPasteboard stringForType:SNNodeTypePasteboard];
    NSPoint view=[self convertPoint:sender.draggingLocation fromView:nil];
    if(nodeType) {
        int type=nodeType.intValue;
        const NSPoint at=[_editor world:view];
        _droppingLibraryNode=YES;
        const BOOL placed=[_editor insertNodeType:type atPoint:NSMakePoint(at.x-cardWidth/2,at.y-40)]!=0;
        _droppingLibraryNode=NO;
        [self clearPlacementFeedback];
        return placed;
    }
    [self clearPlacementFeedback];
    NSArray<NSURL *> *urls=[self draggedURLs:sender];
    if(!urls.count)return NO;
    BOOL clipMode=([NSEvent modifierFlags]&NSEventModifierFlagOption)!=0;
    return [_editor dropFileURLs:urls atPoint:[_editor world:view] clipMode:clipMode];
}
-(BOOL)acceptsFirstResponder{return YES;}
-(void)drawAudioPreview:(NSDictionary *)asset rect:(NSRect)rect {
    [[NSColor colorWithWhite:.11 alpha:1] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:rect xRadius:4 yRadius:4] fill];
    NSString *encoded=asset[@"pcm"];
    const NSUInteger channels=[asset[@"channels"] unsignedIntegerValue];
    if(![encoded isKindOfClass:NSString.class]||!encoded.length||(channels!=1&&channels!=2)) {
        drawText(SNLocalized(@"未载入素材"),NSInsetRect(rect,6,6),NSColor.lightGrayColor,10);
        return;
    }
    if(!_waveforms)_waveforms=[NSMapTable mapTableWithKeyOptions:NSPointerFunctionsWeakMemory|NSPointerFunctionsObjectPointerPersonality
        valueOptions:NSPointerFunctionsStrongMemory];
    NSData *envelope=[_waveforms objectForKey:asset];
    if(!envelope && !_waveformPending) {
        _waveformPending=YES;
        __weak SNInstanceCanvas *weak=self;
        // Only a visible asset can queue a job. Keep the PCM decode off the
        // editor thread and bound the number of in-flight decodes per canvas.
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
            NSData *result=waveformEnvelope(encoded,channels);
            dispatch_async(dispatch_get_main_queue(),^{
                SNInstanceCanvas *canvas=weak;
                if(!canvas)return;
                [canvas->_waveforms setObject:result forKey:asset];
                canvas->_waveformPending=NO;
                canvas.needsDisplay=YES;
            });
        });
    }
    if(!envelope.length)return;
    const float (*peaks)[2]=static_cast<const float (*)[2]>(envelope.bytes);
    NSBezierPath *wave=[NSBezierPath new];wave.lineWidth=1;
    const CGFloat middle=NSMidY(rect),height=NSHeight(rect)*.43;
    for(NSUInteger i=0;i<waveformColumns;++i) {
        const CGFloat x=NSMinX(rect)+3+(i+.5)*(NSWidth(rect)-6)/waveformColumns;
        [wave moveToPoint:NSMakePoint(x,middle-std::max(.025f,peaks[i][1])*height)];
        [wave lineToPoint:NSMakePoint(x,middle-std::min(-.025f,peaks[i][0])*height)];
    }
    [signalColor(tri::Signal::Audio) setStroke];[wave stroke];
}
-(NSDictionary *)wireTargetAt:(NSPoint)point signal:(tri::Signal)signal port:(int *)port {
    SNInstanceEditor *editor=_editor;
    double nearest=14/editor.zoom;
    NSDictionary *target=nil;
    for(NSDictionary *node in [editor.document[@"nodes"] reverseObjectEnumerator]) {
        const auto *spec=tri::nodeSpec(typeOf(node));
        if(!spec)continue;
        for(int p=0;p<int(spec->inputs.size());++p) {
            const auto& input=spec->inputs[p];
            if(input.signal!=signal)continue;
            if([node[@"id"] unsignedLongLongValue]==editor.wireSource && !(input.flags&tri::PortFeedback))continue;
            NSPoint position=jack([self cardRectForNode:node],false,p);
            const double distance=std::hypot(position.x-point.x,position.y-point.y);
            if(distance>=nearest)continue;
            nearest=distance;target=node;*port=p;
        }
    }
    return target;
}
-(void)drawZoneColors:(const Zones&)zones {
    static NSColor *base=[NSColor colorWithSRGBRed:.09 green:.09 blue:.10 alpha:1];
    if(zones.empty){[base setFill];NSRectFill(self.bounds);return;}
    static NSArray<NSColor *> *tints=@[
        [base blendedColorWithFraction:.17 ofColor:stageAccent(0)],
        [base blendedColorWithFraction:.17 ofColor:stageAccent(1)],
        [base blendedColorWithFraction:.17 ofColor:stageAccent(2)],
        [base blendedColorWithFraction:.17 ofColor:stageAccent(3)]];
    CGContextRef context=NSGraphicsContext.currentContext.CGContext;
    const NSRect visible=NSIntersectionRect(self.bounds,CGContextGetClipBoundingBox(context));
    if(NSIsEmptyRect(visible))return;
    const double minX=NSMinX(visible),maxX=NSMaxX(visible);
    const double zoom=_editor.zoom,origin=_editor.origin.x;
    const auto blendWidth=[](const auto& band){return std::min(72.,(band.right-band.left)*.26);};
    const auto stripe=[&](double left,double right) {
        return CGRectMake(left,NSMinY(visible),right-left,NSHeight(visible));
    };
    const auto fill=[&](int stage,double left,double right) {
        if(right<=left)return;
        CGContextSetFillColorWithColor(context,tints[stage].CGColor);
        CGContextFillRect(context,stripe(left,right));
    };

    // Paint solid spans directly. Only visible transitions need a gradient,
    // and the same color pair can be reused at any zoom or scroll position.
    CGContextSaveGState(context);
    CGContextSetShouldAntialias(context,false);
    double cursor=minX;
    int previous=-1;
    for(int i=0;i<stageCount;++i) {
        if(!zones.bands[i].occupied)continue;
        if(previous>=0) {
            const auto& a=zones.bands[previous];
            const auto& b=zones.bands[i];
            const double from=origin+(a.right-blendWidth(a))*zoom,to=origin+(b.left+blendWidth(b))*zoom;
            const double start=std::clamp(from,minX,maxX),end=std::clamp(to,minX,maxX);
            fill(previous,cursor,start);
            if(end>start) {
                auto& blend=_zoneBlends[previous][i];
                if(!blend) {
                    NSArray *colors=@[(__bridge id)tints[previous].CGColor,(__bridge id)tints[i].CGColor];
                    blend=CGGradientCreateWithColors(nullptr,(__bridge CFArrayRef)colors,nullptr);
                }
                CGContextSaveGState(context);
                CGContextClipToRect(context,stripe(start,end));
                CGContextDrawLinearGradient(context,blend,CGPointMake(from,0),CGPointMake(to,0),
                    kCGGradientDrawsBeforeStartLocation|kCGGradientDrawsAfterEndLocation);
                CGContextRestoreGState(context);
            }
            cursor=end;
        }
        previous=i;
    }
    fill(previous,cursor,maxX);
    CGContextRestoreGState(context);
}
-(void)drawZoneTitles:(const Zones&)zones {
    for(int i=0;i<stageCount;++i) {
        const auto& band=zones.bands[i];
        if(!band.occupied)continue;
        const double width=(band.right-band.left)*_editor.zoom;
        if(width<27)continue;
        const double center=_editor.origin.x+band.center()*_editor.zoom;
        drawText(stageTitle(i),NSMakeRect(center-width/2,NSMinY(self.bounds)+7,width,16),
                 stageAccent(i),11,NSTextAlignmentCenter);
    }
}
-(void)drawPlacementPreviewIn:(NSRect)visible {
    if(NSIsEmptyRect(_placementPreview))return;
    NSColor *accent=stageAccent(int(_previewStage));
    NSPoint center=NSMakePoint(NSMidX(_placementPreview),NSMidY(_placementPreview));
    const BOOL onScreen=NSIntersectsRect(visible,_placementPreview);
    if(!onScreen) {
        center.x=std::clamp(center.x,NSMinX(visible),NSMaxX(visible));
        center.y=std::clamp(center.y,NSMinY(visible),NSMaxY(visible));
    }
    NSGradient *glow=[[NSGradient alloc]initWithStartingColor:[accent colorWithAlphaComponent:.33]
        endingColor:[accent colorWithAlphaComponent:0]];
    [glow drawFromCenter:center radius:0 toCenter:center radius:std::max(NSWidth(_placementPreview),NSHeight(_placementPreview))*.67
        options:0];
    if(onScreen) {
        [[accent colorWithAlphaComponent:.12] setFill];
        [[NSBezierPath bezierPathWithRoundedRect:_placementPreview xRadius:8 yRadius:8] fill];
    }
}
-(void)drawRect:(NSRect)dirty {
    SNInstanceEditor *editor=_editor;
    [self refreshLayout];
    const Zones& zones=_previewZones?*_previewZones:(editor.moving && _dragZones?*_dragZones:_zones);
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:self.bounds] addClip];
    [self drawZoneColors:zones];
    NSRect content=self.bounds;
    if(!zones.empty){content.origin.y+=zoneHeaderHeight;content.size.height=std::max(0.,content.size.height-zoneHeaderHeight);}
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:content] addClip];
    NSAffineTransform *transform=[NSAffineTransform transform];
    [transform translateXBy:editor.origin.x yBy:editor.origin.y];[transform scaleBy:editor.zoom];[transform concat];
    const NSPoint visibleOrigin=[editor world:content.origin];
    const NSRect visible=NSInsetRect(NSMakeRect(visibleOrigin.x,visibleOrigin.y,NSWidth(content)/editor.zoom,
        NSHeight(content)/editor.zoom),-8,-8);
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
        NSRect r=groupRect(group);
        if(_dragPreviewShifts)r=shiftedGroupRect(r,_renderNodes,*_dragPreviewShifts);
        if(!NSIntersectsRect(visible,r))continue;
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
    _frameRects.resize(_renderNodes.size());
    const bool inMotion=_dragPreviewShifts || editor.moving ||
        (_placementStarts.count && _placementDocument==editor.document);
    for(size_t i=0;i<_renderNodes.size();++i) {
        const auto& entry=_renderNodes[i];
        _frameRects[i]=inMotion?[self displayRect:entry.rect forNode:entry.node stage:entry.stage]:entry.rect;
    }
    for(const auto& edge:_renderCables) {
        const NSPoint from=jack(_frameRects[edge.source],true,edge.output);
        const NSPoint to=jack(_frameRects[edge.target],false,edge.input);
        // The cubic stays in the convex hull of its four control points.
        // Keep crossing cables even when both of their cards are offscreen.
        if(std::max(from.x+70,to.x)<NSMinX(visible) || std::min(from.x,to.x-70)>NSMaxX(visible) ||
           std::max(from.y,to.y)<NSMinY(visible) || std::min(from.y,to.y)>NSMaxY(visible))continue;
        const BOOL chosen=edge.index==editor.selectedEdge;
        drawCable(from,to,chosen?NSColor.whiteColor:signalColor(edge.signal),chosen?3:2);
    }
    [self drawPlacementPreviewIn:visible];
    for(size_t i=0;i<_renderNodes.size();++i) {
        const NSRect r=_frameRects[i];
        if(!NSIntersectsRect(visible,r))continue;
        NSDictionary *node=_renderNodes[i].node;
        const int type=_renderNodes[i].type;
        const BOOL bypassed=[node[@"bypass"] boolValue];
        [[NSColor colorWithWhite:bypassed?.13:.18 alpha:1]setFill];[[NSBezierPath bezierPathWithRoundedRect:r xRadius:4 yRadius:4]fill];
        [[editor isSelected:[node[@"id"] unsignedLongLongValue]]?NSColor.whiteColor:[NSColor colorWithWhite:.4 alpha:1] setStroke];
        [[NSBezierPath bezierPathWithRect:r]stroke];
        if(const auto *spec=tri::nodeSpec(type);spec && isFixedAudioClip(*spec)) {
            const NSRect accent=NSMakeRect(NSMinX(r)+2,NSMinY(r)+1,NSWidth(r)-4,2);
            NSGradient *clipTint=[[NSGradient alloc]initWithStartingColor:stageAccent(int(Stage::Notes))
                endingColor:stageAccent(int(Stage::Sources))];
            [clipTint drawInRect:accent angle:0];
        }
        drawText([NSString stringWithFormat:@"%@ #%@",nodeLabel(node),node[@"id"]],
            NSMakeRect(r.origin.x+9,r.origin.y+8,bypassed?112:154,20),NSColor.whiteColor);
        if(bypassed)drawText(SNLocalized(@"旁通"),NSMakeRect(NSMaxX(r)-46,r.origin.y+8,40,20),NSColor.systemOrangeColor,10);
        if(type==0) {
            [[NSColor systemRedColor]setStroke];
            NSBezierPath *warn=[NSBezierPath bezierPathWithRect:r];[warn stroke];
            drawText(node[@"nodeKey"] ?: SNLocalized(@"未知类型"),NSMakeRect(r.origin.x+9,r.origin.y+30,150,18),
                     NSColor.systemRedColor,10);
        }
        auto inputs=tri::inputTypes(type);
        for(int p=0;p<(int)inputs.size();++p) {
            NSPoint at=jack(r,false,p);[signalColor(inputs[p])setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(at.x-5,at.y-5,10,10)]fill];
            drawText(portLabel(type,true,p),NSMakeRect(at.x+9,at.y-8,73,18),NSColor.lightGrayColor,10);
        }
        int outputs=tri::outputCount(type);
        for(int p=0;p<outputs;++p) {
            NSPoint at=jack(r,true,p);[signalColor(tri::outputType(type,p))setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(at.x-5,at.y-5,10,10)]fill];
            drawText(portLabel(type,false,p),NSMakeRect(at.x-82,at.y-8,73,18),NSColor.lightGrayColor,10,NSTextAlignmentRight);
        }
        if(const auto *spec=tri::nodeSpec(type)) {
            double y=NSMinY(r)+cardDetailsTop(type);
            int count=0;
            for(int p:spec->params) {
                if(!canvasParameter(*spec,p))continue;
                NSString *value=canvasParameterValue(type,p,node);
                const CGFloat valueWidth=std::clamp([value sizeWithAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:10]}].width+3,44.0,86.0);
                drawText(parameterName(type,p),NSMakeRect(NSMinX(r)+9,y,150-valueWidth,16),
                    [NSColor colorWithWhite:.67 alpha:1],10);
                drawText(value,NSMakeRect(NSMaxX(r)-9-valueWidth,y,valueWidth,16),
                    [NSColor colorWithWhite:.9 alpha:1],10,NSTextAlignmentRight);
                y+=16;
                if(++count==2)break;
            }
            if(spec->environmentRole==tri::EnvironmentRole::Timecode)
                drawText(editor.processingLabel.stringValue,NSMakeRect(NSMinX(r)+9,y,154,16),
                    editor.processingLabel.textColor ?: NSColor.lightGrayColor,10);
            if(hasAudioPreview(*spec)) {
                NSRect preview=NSMakeRect(NSMinX(r)+9,y+2,154,29);
                [self drawAudioPreview:node[@"asset"] rect:preview];
            }
        }
    }
    if(editor.wireSource) {
        NSDictionary *source=[editor node:editor.wireSource];
        const auto *port=source?tri::nodePort(typeOf(source),false,size_t(editor.wirePort)):nullptr;
        if(port) {
            int targetPort=0;
            NSDictionary *target=[self wireTargetAt:[editor world:editor.wireCursor] signal:port->signal port:&targetPort];
            const NSPoint to=target?jack([self cardRectForNode:target],false,targetPort):[editor world:editor.wireCursor];
            NSColor *color=signalColor(port->signal);
            drawCable(jack([self cardRectForNode:source],true,editor.wirePort),to,color,2.5);
            if(target) {
                const CGFloat radius=8/editor.zoom;
                NSBezierPath *ring=[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(to.x-radius,to.y-radius,2*radius,2*radius)];
                [color setStroke];ring.lineWidth=1.5/editor.zoom;[ring stroke];
            }
        }
    }
    if(editor.boxSelecting) {
        NSRect box=NSMakeRect(std::min(editor.boxOrigin.x,editor.boxCurrent.x),std::min(editor.boxOrigin.y,editor.boxCurrent.y),
                              std::fabs(editor.boxCurrent.x-editor.boxOrigin.x),std::fabs(editor.boxCurrent.y-editor.boxOrigin.y));
        NSBezierPath *path=[NSBezierPath bezierPathWithRect:box];[path setLineDash:(CGFloat[]){6,4} count:2 phase:0];
        path.lineWidth=1.5;[NSColor.whiteColor setStroke];[path stroke];
    }
    [NSGraphicsContext restoreGraphicsState];
    [self drawZoneTitles:zones];
    if(_placementBlocked)
        drawText(SNLocalized(@"区域太窄，按住 Shift 放置可展开。"),
                 NSMakeRect(NSMinX(self.bounds)+10,NSMinY(self.bounds)+zoneHeaderHeight+5,NSWidth(self.bounds)-20,20),
                 NSColor.systemOrangeColor,11,NSTextAlignmentCenter);
    [NSGraphicsContext restoreGraphicsState];
}
-(void)rightMouseDown:(NSEvent *)event {
    _cameraEnd=_cameraStart;
    SNInstanceEditor *editor=_editor;
    editor.menuPoint=[editor world:[self convertPoint:event.locationInWindow fromView:nil]];
    NSMenu *menu=[NSMenu new];
    const auto types=tri::nodeCatalog();
    NSString *lastSection=nil;
    for(NSString *group in libraryCategories(types)) {
        NSMenu *items=[NSMenu new];
        items.delegate=self;
        NSString *groupTitle=nil;
        for(int type:types)if([@(tri::nodeCategory(type)) isEqual:group]) {
            groupTitle=SNNodeLocalized(type,group);
            NSMenuItem *item=[items addItemWithTitle:title(type) action:@selector(insert:) keyEquivalent:@""];
            item.target=editor;item.tag=type;
        }
        if(!items.numberOfItems)continue;
        NSString *section=librarySection(group);
        if(lastSection && ![lastSection isEqual:section])[menu addItem:[NSMenuItem separatorItem]];
        lastSection=section;
        [menu addItemWithTitle:groupTitle action:nil keyEquivalent:@""].submenu=items;
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
    [self clearPlacementFeedback];
}
-(void)menu:(NSMenu *)menu willHighlightItem:(NSMenuItem *)item {
    if(item.action==@selector(insert:))[self previewPlacementForType:int(item.tag) at:_editor.menuPoint libraryDrag:NO];
    else [self clearPlacementFeedback];
}
-(void)menuDidClose:(NSMenu *)menu {
    [self clearPlacementFeedback];
}
-(void)mouseDown:(NSEvent *)event {
    _cameraEnd=_cameraStart;
    SNInstanceEditor *editor=_editor;[self.window makeFirstResponder:self];
    NSPoint view=[self convertPoint:event.locationInWindow fromView:nil],point=[editor world:view];
    editor.lastPoint=view;
    _dragZones.reset();
    _dragOrigins=nil;_dragPositions=nil;
    [self clearPlacementFeedback];
    [self refreshLayout];
    if(view.y<NSMinY(self.bounds)+zoneHeaderHeight && !_zones.empty) {
        editor.panning=YES;
        return;
    }
    BOOL additive=(event.modifierFlags&(NSEventModifierFlagShift|NSEventModifierFlagCommand))!=0;
    for(NSDictionary *node in [editor.document[@"nodes"] reverseObjectEnumerator]) {
        int type=typeOf(node);
        const NSRect rect=[self cardRectForNode:node];
        int outputs=tri::outputCount(type);
        for(int p=0;p<outputs;++p) {
            NSPoint pos=jack(rect,true,p);
            if(std::hypot(pos.x-point.x,pos.y-point.y)<12/editor.zoom) {
                editor.wireSource=[node[@"id"] unsignedLongLongValue];editor.wirePort=p;
                editor.wireCursor=view;self.needsDisplay=YES;return;
            }
        }
        if(NSPointInRect(point,rect)) {
            _nodeGrab=point;
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
            if(editor.moving) {
                NSMutableDictionary *origins=[NSMutableDictionary new];
                for(NSNumber *key in [editor activeIDs]) {
                    NSDictionary *selected=[editor node:key.unsignedLongLongValue];
                    if(selected)origins[key]=[NSValue valueWithPoint:nodeRect(selected).origin];
                }
                _dragOrigins=origins;
            }
            self.needsDisplay=YES;return;
        }
    }
    NSInteger index=0;
    for(NSDictionary *edge in editor.document[@"edges"]) {
        NSDictionary *a=[editor node:[edge[@"source"] unsignedLongLongValue]],*b=[editor node:[edge[@"target"] unsignedLongLongValue]];
        if(a && b && nearEdge(jack([self cardRectForNode:a],true,outputPortOf(a,edge)),
            jack([self cardRectForNode:b],false,inputPortOf(b,edge)),point,7/editor.zoom)) {
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
    if(editor.wireSource) {
        editor.wireCursor=view;self.needsDisplay=YES;return;
    }
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
        [self invalidateLayout];
        self.needsDisplay=YES;return;
    }
    if(editor.boxSelecting) {
        editor.boxCurrent=[editor world:view];self.needsDisplay=YES;return;
    }
    double dx=view.x-editor.lastPoint.x,dy=view.y-editor.lastPoint.y;
    if(editor.moving) {
        if(!_dragZones) {
            [self refreshLayout];
            _dragZones=_zones;
            _dragPositions=[NSMutableDictionary new];
        }
        const NSPoint point=[editor world:view];
        for(NSNumber *key in _dragOrigins) {
            const NSPoint start=_dragOrigins[key].pointValue;
            double x=start.x+point.x-_nodeGrab.x;
            double y=start.y+point.y-_nodeGrab.y;
            if(editor.snap){x=std::round(x/20)*20;y=std::round(y/20)*20;}
            _dragPositions[key]=[NSValue valueWithPoint:NSMakePoint(x,y)];
        }
        if(_dragOrigins.count==1) {
            NSDictionary *node=[editor node:editor.moving];
            const Stage stage=canvasStage(typeOf(node));
            const NSRect source=[self cardRectForNode:node];
            const NSRect target=placementRect(*_dragZones,stage,source,editor.document[@"nodes"],editor.moving);
            [self showPlacementPreview:target from:source stage:stage];
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
        const uint64_t sourceID=editor.wireSource;
        const int sourceType=typeOf([editor node:sourceID]),sourcePort=editor.wirePort;
        const auto *output=tri::nodePort(sourceType,false,size_t(sourcePort));
        int targetPort=0;
        NSDictionary *target=output && NSPointInRect([self convertPoint:event.locationInWindow fromView:nil],self.bounds)
            ? [self wireTargetAt:point signal:output->signal port:&targetPort] : nil;
        editor.wireSource=0;self.needsDisplay=YES;
        if(target) {
            const uint64_t targetID=[target[@"id"] unsignedLongLongValue];
            const int targetType=typeOf(target);
            NSMutableArray *edges=editor.document[@"edges"];
            NSMutableDictionary *edge=makeEdge(sourceID,targetID,targetType,targetPort,sourceType,sourcePort);
            if([edges containsObject:edge])return;
            NSDictionary *previous=copyJSON(editor.document);
            const auto *input=tri::nodePort(targetType,true,size_t(targetPort));
            if(!(input->flags&tri::PortMultiSource)) {
                NSIndexSet *existing=[edges indexesOfObjectsPassingTest:^BOOL(NSDictionary *e,NSUInteger,BOOL*){
                    return [e[@"target"] unsignedLongLongValue]==targetID && inputPortOf(target,e)==targetPort;
                }];
                [edges removeObjectsAtIndexes:existing];
            }
            [edges addObject:edge];
            if([editor commit:previous])editor.selectedEdge=-1;
        }
        return;
    }
    if(editor.moving && _dragZones) {
        [self mouseDragged:event];
        const uint64_t uid=editor.moving;
        NSNumber *key=@(uid);
        const NSPoint start=[_dragPositions[key] pointValue];
        const BOOL corrected=[editor node:uid] && !NSIsEmptyRect(_placementPreview);
        if(corrected)_dragPositions[key]=[NSValue valueWithPoint:_placementPreview.origin];
        NSDictionary *previous=copyJSON(editor.document);
        for(NSNumber *dragged in _dragPositions) {
            NSMutableDictionary *node=[editor node:dragged.unsignedLongLongValue];
            const NSPoint point=_dragPositions[dragged].pointValue;
            node[@"x"]=@(point.x);node[@"y"]=@(point.y);
        }
        editor.moving=0;
        if([editor commit:previous] && corrected)
            [self animatePlacements:@{key:[NSValue valueWithPoint:start]} focus:uid];
    }
    [self clearPlacementFeedback];
    _dragZones.reset();
    _dragOrigins=nil;_dragPositions=nil;
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
    if(editor.wireSource) {
        if(event.keyCode==53){editor.wireSource=0;self.needsDisplay=YES;}
        return;
    }
    if(editor.moving && _dragZones && event.keyCode==53) {
        editor.moving=0;
        _dragZones.reset();
        _dragOrigins=nil;_dragPositions=nil;
        [self clearPlacementFeedback];
        self.needsDisplay=YES;
        return;
    }
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
@property(copy) TRINodeLibraryRefresh refreshHandler;
- (void)rebuild;
- (void)setSearchQuery:(NSString *)query;
- (void)toggleCategory:(NSString *)category;
@end
@implementation SNNodeLibrary {
    NSMutableArray<NSDictionary *> *_rows;
    NSMutableSet<NSString *> *_collapsedCategories;
    NSSearchField *_searchField;
    NSButton *_expandButton, *_collapseButton, *_refreshButton;
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
        _refreshButton=[NSButton buttonWithTitle:@"" target:self action:@selector(refreshNodes:)];
        for(NSButton *button in @[_expandButton,_collapseButton,_refreshButton]) {
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
-(NSString *)query {
    return [_searchField.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
}
-(BOOL)matchesType:(int)type category:(NSString *)category query:(NSString *)query {
    if(!query.length)return YES;
    NSString *name=@(tri::nodeTitle(type));
    NSString *section=librarySection(category);
    NSString *searchable=[NSString stringWithFormat:@"%@ %@ %@ %@ %@ %@ %s %d",
        SNNodeLocalizedForLanguage(type,name,NO),SNNodeLocalizedForLanguage(type,name,YES),
        SNNodeLocalizedForLanguage(type,category,NO),SNNodeLocalizedForLanguage(type,category,YES),
        section,SNLocalizedForLanguage(section,YES),tri::nodeKey(type),type];
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
    _refreshButton.title=SNLocalized(@"刷新");
    _refreshButton.toolTip=SNLocalized(@"刷新节点库");
    _expandButton.enabled=!query.length;_collapseButton.enabled=!query.length;
    double y=108;
    // Iterate descriptors directly: search remains bilingual even when the
    // canvas/menu catalogue has already translated its display strings.
    const auto types=tri::nodeCatalog();
    NSString *lastSection=nil;
    for(NSString *category in libraryCategories(types)) {
        NSMutableArray<NSNumber *> *items=[NSMutableArray new];
        for(int type:types) {
            if([@(tri::nodeCategory(type)) isEqualToString:category] && [self matchesType:type category:category query:query])
                [items addObject:@(type)];
        }
        if(!items.count)continue;
        NSString *section=librarySection(category);
        if(![section isEqualToString:lastSection]) {
            if(lastSection)y+=7;
            [_rows addObject:@{@"kind":@"section",@"title":SNLocalized(section),@"section":section,@"y":@(y),@"h":@25}];
            y+=25;lastSection=section;
        }
        _matchCount+=items.count;
        BOOL collapsed=!query.length && [_collapsedCategories containsObject:category];
        [_rows addObject:@{@"kind":@"header",@"title":SNNodeLocalized(items.firstObject.intValue,category),@"category":category,
            @"section":section,@"count":@(items.count),@"collapsed":@(collapsed),@"y":@(y),@"h":@28}];
        y+=30;
        if(!collapsed)for(NSNumber *type in items) {
            [_rows addObject:@{@"kind":@"item",@"title":SNNodeLocalized(type.intValue,@(tri::nodeTitle(type.intValue))),
                @"type":type,@"category":category,@"section":section,@"y":@(y),@"h":@28}];
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
    _refreshButton.frame=NSMakeRect(size.width-76,7,68,23);
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
-(void)refreshNodes:(id)sender {
    [self rebuild];
    if(!_refreshHandler) {
        [_editor message:SNLocalized(@"节点库已刷新。")];
        return;
    }
    _refreshButton.enabled=NO;
    [_editor message:SNLocalized(@"正在检查节点更新…")];
    __weak SNNodeLibrary *weak=self;
    _refreshHandler(^(NSString *message) {
        SNNodeLibrary *library=weak;
        if(!library)return;
        [library rebuild];
        library->_refreshButton.enabled=YES;
        [library.editor message:message];
    });
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
    for(int type:tri::nodeCatalog())[_collapsedCategories addObject:@(tri::nodeCategory(type))];
    [self saveCollapsedCategories];[self rebuild];
}
-(NSInteger)rowAtPoint:(NSPoint)p {
    if(p.x<4||p.x>NSWidth(self.bounds)-4)return -1;
    for(NSUInteger i=0;i<_rows.count;++i) {
        NSDictionary *row=_rows[i];
        double y=[row[@"y"] doubleValue],h=[row[@"h"] doubleValue];
        if(p.y>=y && p.y<y+h)return [row[@"kind"] isEqualToString:@"section"]?-1:(NSInteger)i;
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
    [self drawLibraryText:SNLocalized(@"节点库") rect:NSMakeRect(12,11,NSWidth(self.bounds)-120,18)
        color:[NSColor colorWithWhite:.9 alpha:1] size:12];
    [self drawLibraryText:[NSString stringWithFormat:@"%lu",(unsigned long)_matchCount]
        rect:NSMakeRect(NSWidth(self.bounds)-108,12,30,16) color:[NSColor colorWithWhite:.5 alpha:1] size:11];
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
        NSColor *tint=libraryTint(row[@"section"]);
        BOOL section=[row[@"kind"] isEqualToString:@"section"];
        BOOL header=[row[@"kind"] isEqualToString:@"header"];
        BOOL hot=((NSInteger)i==_hover);
        [[tint colorWithAlphaComponent:hot?.22:section?.20:header?.12:.055]setFill];
        [[NSBezierPath bezierPathWithRoundedRect:r xRadius:4 yRadius:4]fill];
        if(section) {
            [self drawLibraryText:row[@"title"] rect:NSMakeRect(12,y+5,r.size.width-14,17)
                color:tint size:11];
            continue;
        }
        if(header) {
            [self drawLibraryText:[row[@"collapsed"] boolValue]?@"▸":@"▾" rect:NSMakeRect(11,y+5,12,18)
                color:[NSColor colorWithWhite:.6 alpha:1] size:12];
            [self drawLibraryText:row[@"title"] rect:NSMakeRect(27,y+6,r.size.width-65,17)
                color:[NSColor colorWithWhite:.88 alpha:1] size:11];
            [self drawLibraryText:[row[@"count"] stringValue] rect:NSMakeRect(NSWidth(self.bounds)-33,y+6,24,17)
                color:[NSColor colorWithWhite:.5 alpha:1] size:10.5];
            continue;
        }
        [tint setFill];
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

NSView *TRICreateEditor(TRIRead read,TRIWrite write,TRIMIDI midi,TRIInputRead inputRead,TRIInputWrite inputWrite,TRIStatus status,TRIAssetsRead assetsRead,TRIAssetsWrite assetsWrite,TRIProcessingRead processingRead,TRIProcessingWrite processingWrite,TRINodeStop nodeStop,TRIEnvironmentRead environmentRead,TRIMidiCapture midiCapture) {
    SNInstanceEditor *editor=[[SNInstanceEditor alloc]initWithFrame:NSMakeRect(0,0,1360,840)];
    editor.catalogGeneration=tri::nodeCatalogSnapshot()->generation;
    editor.read=read;editor.write=write;editor.midi=midi;editor.inputRead=inputRead;editor.inputWrite=inputWrite;
    editor.nodeStop=nodeStop;editor.environmentRead=environmentRead;
    editor.hostManaged=environmentRead&&[environmentRead()[@"hostManaged"] boolValue];
    editor.midiCapture=midiCapture;
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
    NSTextField *heading=[NSTextField labelWithString:@"Soundnet 3.0.0"];
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
        editor.document=tri::newProjectDocument();
        if(assetsWrite)assetsWrite(@{@"graph":editor.document});
    }
    [editor refreshLanguage:nil];
    [editor layoutUI];
    [editor updateZoomLabel];
    [editor fit:nil];[editor inspect];
    __weak SNInstanceEditor *weak=editor;
    editor.timer=[NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer*){[weak tick];}];
    return editor;
}

BOOL TRIOpenEditorProject(NSView *view,NSURL *url) {
    return [view isKindOfClass:SNInstanceEditor.class] && [(SNInstanceEditor *)view openProject:url];
}

void TRIFlushEditorAutosave(NSView *view) {
    if([view isKindOfClass:SNInstanceEditor.class])[(SNInstanceEditor *)view finishAutosave];
}

void TRISetNodeLibraryRefresh(NSView *view,TRINodeLibraryRefresh refresh) {
    if([view isKindOfClass:SNInstanceEditor.class]) {
        SNNodeLibrary *library=(SNNodeLibrary *)[(SNInstanceEditor *)view library];
        library.refreshHandler=refresh;
    }
}

BOOL TRIRefreshNodeCatalog(NSView *view,NSString **why) {
    if(!NSThread.isMainThread||![view isKindOfClass:SNInstanceEditor.class]) {
        if(why)*why=SNLocalized(@"节点刷新必须在编辑器线程运行。");return NO;
    }
    SNInstanceEditor *editor=(SNInstanceEditor *)view;
    editor.catalogGeneration=tri::nodeCatalogSnapshot()->generation;
    [editor stopGraph:nil];
    [editor beginTaskSession];
    for(NSView *panel in editor.inspector.subviews.copy)[panel removeFromSuperview];
    NSDictionary *previous=copyJSON(editor.document);
    NSString *error=nil;
    NSDictionary *canonical=triassets::canonicalGraph(editor.document,&error);
    if(canonical)editor.document=copyJSON(canonical);
    const BOOL applied=canonical&&[editor commit:previous];
    if(!applied) {
        error=error ?: editor.status.stringValue;
        if(why)*why=error;
        [editor message:error];
    }
    [(SNNodeLibrary *)editor.library rebuild];
    [editor inspect];editor.canvas.needsDisplay=YES;
    return applied;
}

BOOL TRIPrepareForNodeRefresh(NSView *view,NSString **why) {
    return [view isKindOfClass:SNInstanceEditor.class]&&
           [(SNInstanceEditor *)view prepareForNodeRefresh:why];
}
