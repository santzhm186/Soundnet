#pragma once
// MusicGen — shared inspector panel for the `soundnet.yue2` and
// `soundnet.ace-step` nodes.
//
// Layout: status + progress spinner, Generate/Stop, the take list (each entry
// selects audio and score resources through the normal commit transaction) and an
// audition transport (play/pause/stop plus a draggable seek bar). The
// audition player is deliberately independent of the graph: in-graph playback
// stays driven by the node's trigger port. Objective-C++: UI translation
// units only.
#import <Cocoa/Cocoa.h>
#import <AVFoundation/AVFoundation.h>
#include "MusicGenBackend.hpp"
#include "GenerationArchive.hpp"
#include "../NodeEditor.h"
#import <objc/runtime.h>

// The standalone editor and the AU editor may coexist in one process.
#ifdef SNInstanceEditor
#define SNMusicGenPanel SNAUMusicGenPanel
#endif

namespace tri::musicgen {

struct PanelSpec {
    // Node-specific behaviour, supplied by each node's Editor.hpp.
    std::function<GenRequest(NSDictionary* node,NSDictionary* environment,
                             NSString* workDir,bool feedback)> request;
    std::function<soundnet::TaskService::Job(GenRequest,NSString* label)> job;
    std::function<bool(NodeEditorContext,NSDictionary*,NSString**)> importScore;
    std::function<NSDictionary*(const soundnet::TaskResult&,NSDictionary*)> apply;
    NSString* defaultLabel=@"Take";     // take name fallback
    NSString* promptKey=@"";            // texts key of the prompt
    NSString* promptMissing=@"";        // localized message when the prompt is empty
    NSString* scoreKey=@"";             // texts key of the ABC score ("" = none)
};
} // namespace tri::musicgen

@interface SNMusicGenPanel : NSObject {
    @public tri::NodeEditorContext context;
    @private tri::musicgen::PanelSpec spec;
}
@property(nonatomic,strong) NSDictionary* configuration;
@property(nonatomic,strong) NSTextField* status;
@property(nonatomic,strong) NSProgressIndicator* spinner;
@property(nonatomic,strong) NSButton* generateButton;
@property(nonatomic,strong) NSButton* stopButton;
@property(nonatomic,strong) NSButton* feedbackButton;
@property(nonatomic,strong) NSPopUpButton* takePopup;
@property(nonatomic,strong) NSButton* playButton;
@property(nonatomic,strong) NSButton* pauseButton;
@property(nonatomic,strong) NSButton* stopPlaybackButton;
@property(nonatomic,strong) NSSlider* seek;
@property(nonatomic,strong) NSTextField* timeField;
@property(nonatomic,strong) AVAudioPlayer* player;
@property(nonatomic,strong) NSTimer* timer;
@property(nonatomic,copy) NSString* auditionFingerprint;
-(void)run:(id)sender;
-(void)runFromScore:(id)sender;
-(void)importScore:(id)sender;
-(void)cancel:(id)sender;
-(void)takeChanged:(id)sender;
-(void)play:(id)sender;
-(void)pause:(id)sender;
-(void)stopPlayback:(id)sender;
-(void)seekChanged:(id)sender;
-(void)tick:(id)sender;
-(void)refresh;
@end

@implementation SNMusicGenPanel

-(instancetype)initWithContext:(tri::NodeEditorContext)editorContext
                          spec:(tri::musicgen::PanelSpec)panelSpec
                   configuration:(NSDictionary*)node {
    self=[super init];
    if(self) {
        context=editorContext;
        spec=panelSpec;
        _configuration=[node copy];
    }
    return self;
}

-(void)dealloc {
    [_timer invalidate];
}

-(void)submitWithFeedback:(BOOL)feedback {
    __attribute__((objc_precise_lifetime)) SNMusicGenPanel *keepAlive=self;
    (void)keepAlive; // committing an active field can rebuild the inspector
    if(!context.submitTask)return;
    tri::NodeEditorContext current;
    if(!context.captureTaskContext||!context.captureTaskContext(current)) {
        _status.stringValue=context.text(@"无法读取节点参数，请重新选择节点。");
        return;
    }
    NSDictionary* node=current.configuration;
    if(!feedback) {
        NSString* prompt=node[@"texts"][spec.promptKey];
        if(![prompt isKindOfClass:NSString.class]||!prompt.length) {
            _status.stringValue=spec.promptMissing.length?spec.promptMissing:context.text(@"请先填写风格描述。");
            return;
        }
    } else {
        NSString* score=spec.scoreKey.length?node[@"texts"][spec.scoreKey]:nil;
        if(![score isKindOfClass:NSString.class]||!score.length) {
            _status.stringValue=context.text(@"乐谱为空，请先生成或粘贴 ABC 文本。");
            return;
        }
    }
    tri::musicgen::GenRequest request=spec.request(node,current.environment,@"",feedback);
    NSString* rawLabel=node[@"name"];
    NSString* nodeLabel=[rawLabel isKindOfClass:NSString.class]&&rawLabel.length?rawLabel:spec.defaultLabel;
    std::string why;
    soundnet::TaskService::Job job=spec.job(std::move(request),nodeLabel);
    if(current.submitTask(std::move(job),spec.apply,why)) {
        _status.stringValue=context.text(@"正在生成…");
        [self startPolling];
        [self refresh];
    } else _status.stringValue=[NSString stringWithUTF8String:why.c_str()];
}

-(void)startPolling {
    if(_timer)return;
    // Block-based timer: NSTimer retains its target, and self retains the
    // timer, so a strong target would leak the panel forever.
    __weak SNMusicGenPanel* weak=self;
    _timer=[NSTimer scheduledTimerWithTimeInterval:0.25 repeats:YES block:^(NSTimer*) {
        [weak tick:nil];
    }];
}

-(void)tick:(id)sender {
    soundnet::TaskService::Snapshot snapshot;
    const bool found=context.taskSnapshot&&context.taskSnapshot(snapshot);
    if(found&&snapshot.active()) {
        _status.stringValue=snapshot.cancellationRequested?context.text(@"正在取消生成…"):
            (snapshot.progress>=0?[NSString stringWithFormat:context.text(@"正在生成 %d%%"),snapshot.progress]
             :context.text(@"正在生成…"));
        [_spinner startAnimation:nil];
        [self refresh];
        return;
    }
    if(!self.player.isPlaying) {[_timer invalidate];_timer=nil;}
    [_spinner stopAnimation:nil];
    _status.stringValue=(found&&!snapshot.message.empty())?
        [NSString stringWithUTF8String:snapshot.message.c_str()]:context.text(@"空闲");
    [self refresh];
}

-(void)run:(id)sender {[self submitWithFeedback:NO];}
-(void)runFromScore:(id)sender {[self submitWithFeedback:YES];}
-(void)cancel:(id)sender {if(context.cancelTask)context.cancelTask();[self tick:nil];}

// Selection resolves immutable platform resources, including score-only takes.
-(void)takeChanged:(id)sender {
    __attribute__((objc_precise_lifetime)) SNMusicGenPanel *keepAlive=self;
    (void)keepAlive; // commit synchronously replaces this panel
    tri::NodeEditorContext current;
    if(!context.captureTaskContext||!context.captureTaskContext(current)||!current.commit)return;
    NSDictionary* node=current.configuration;
    NSArray* takes=tri::projectresources::collection(node,@"generation.takes");
    const NSUInteger index=(NSUInteger)self.takePopup.indexOfSelectedItem-1;
    if(index>=takes.count)return;
    NSDictionary* take=takes[index];
    NSDictionary* audio=tri::projectresources::entryResource(node,take,@"audio");
    NSDictionary* score=tri::projectresources::entryResource(node,take,@"score");
    if(!audio&&!score) {_status.stringValue=context.text(@"该记录的资源不可用。");return;}
    NSMutableDictionary* replacement=[node mutableCopy];
    if(audio)replacement[@"asset"]=audio[@"data"];
    else replacement[@"asset"]=NSNull.null;
    if(spec.scoreKey.length) {
        NSMutableDictionary* texts=[node[@"texts"] mutableCopy] ?: [NSMutableDictionary new];
        if(score)texts[spec.scoreKey]=score[@"text"];
        else [texts removeObjectForKey:spec.scoreKey];
        replacement[@"texts"]=texts;
    }
    current.commit(replacement);
    _status.stringValue=context.text(@"已载入所选记录。");
}
-(void)importScore:(id)sender {
    __attribute__((objc_precise_lifetime)) SNMusicGenPanel *keepAlive=self;
    (void)keepAlive; // createNode may select a new node and destroy this panel
    tri::NodeEditorContext current;
    if(!spec.importScore||!context.captureTaskContext||!context.captureTaskContext(current))return;
    NSString *why=nil;
    if(!spec.importScore(current,current.configuration,&why))
        _status.stringValue=why ?: context.text(@"乐谱无法转换为 MIDI 剪辑。");
}

// Audition transport — independent of the audio graph (no engine state).
-(BOOL)prepareAudition {
    NSDictionary* asset=self.configuration[@"asset"];
    if(![asset isKindOfClass:NSDictionary.class]||![asset[@"pcm"] isKindOfClass:NSString.class]) {
        _status.stringValue=context.text(@"还没有素材可试听。");
        return NO;
    }
    NSData* pcm=[[NSData alloc]initWithBase64EncodedString:asset[@"pcm"] options:0];
    const NSUInteger frames=pcm.length/4/std::max(1,[asset[@"channels"] intValue]);
    if(!frames) {_status.stringValue=context.text(@"还没有素材可试听。");return NO;}
    // A take switch can produce a different asset with the same frame count
    // and rate, so the cache key must include content bytes as well.
    NSData* head=[pcm subdataWithRange:NSMakeRange(0,std::min((NSUInteger)64,pcm.length))];
    const NSUInteger tailStart=pcm.length>64?pcm.length-64:0;
    NSData* tail=[pcm subdataWithRange:NSMakeRange(tailStart,pcm.length-tailStart)];
    NSString* fingerprint=[NSString stringWithFormat:@"%lu:%.0f:%@:%@",
        (unsigned long)pcm.length,[asset[@"rate"] doubleValue],
        [head base64EncodedStringWithOptions:0],[tail base64EncodedStringWithOptions:0]];
    if(self.player&&[fingerprint isEqualToString:self.auditionFingerprint])return YES;
    NSString *why=nil;
    NSData *wav=tri::projectresources::audioFileData(asset,&why);
    if(!wav) {_status.stringValue=why ?: context.text(@"还没有素材可试听。");return NO;}
    self.auditionFingerprint=fingerprint;
    NSError* error=nil;
    AVAudioPlayer* player=[[AVAudioPlayer alloc]initWithData:wav error:&error];
    if(!player) {_status.stringValue=error.localizedDescription?:@"无法播放。";return NO;}
    self.player=player;
    self.seek.maxValue=std::max(0.01,player.duration);
    return YES;
}

-(void)play:(id)sender {
    if(![self prepareAudition])return;
    if([self.player play]) [self startPolling];
    [self refresh];
}
-(void)pause:(id)sender {[self.player pause];[self refresh];}
-(void)stopPlayback:(id)sender {
    [self.player stop];
    self.player.currentTime=0;
    [self refresh];
}
-(void)seekChanged:(id)sender {
    if(!self.player)return;
    self.player.currentTime=self.seek.doubleValue;
    [self updateTransport];
}
-(void)updateTransport {
    if(!self.player)return;
    self.seek.doubleValue=self.player.currentTime;
    const double total=self.player.duration,now=self.player.currentTime;
    self.timeField.stringValue=[NSString stringWithFormat:@"%d:%02d / %d:%02d",
        int(now/60),int(std::fmod(now,60)),int(total/60),int(std::fmod(total,60))];
}
-(void)refresh {
    soundnet::TaskService::Snapshot snapshot;
    const bool busy=context.taskSnapshot&&context.taskSnapshot(snapshot)&&snapshot.active();
    _generateButton.enabled=!busy&&bool(context.submitTask);
    _feedbackButton.enabled=!busy&&bool(context.submitTask);
    _stopButton.enabled=busy&&bool(context.cancelTask);
    const BOOL hasAsset=[self.configuration[@"asset"] isKindOfClass:NSDictionary.class]&&
        [self.configuration[@"asset"][@"pcm"] isKindOfClass:NSString.class];
    _playButton.enabled=hasAsset;
    _pauseButton.enabled=hasAsset;
    _stopPlaybackButton.enabled=hasAsset;
    _seek.enabled=hasAsset;
    if(self.player)[self updateTransport];
    else if(!hasAsset)self.timeField.stringValue=@"";
}

@end

namespace tri::musicgen {

inline NSView* makeNodePanel(NodeEditorContext context,const PanelSpec& spec) {
    NSDictionary* node=context.configuration ?: @{};
    const bool scoreLoop=spec.scoreKey.length>0;
    SNMusicGenPanel* action=[[SNMusicGenPanel alloc]initWithContext:context spec:spec configuration:node];
    const CGFloat height=scoreLoop?360:280;
    NSView* panel=[[NSView alloc]initWithFrame:NSMakeRect(0,0,270,height)];
    static const char owner=0;
    objc_setAssociatedObject(panel,&owner,action,OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    action.status=[NSTextField wrappingLabelWithString:context.text(@"空闲")];
    action.status.frame=NSMakeRect(0,height-44,238,40);
    action.status.font=[NSFont systemFontOfSize:11];
    action.status.textColor=[NSColor secondaryLabelColor];
    [panel addSubview:action.status];
    action.spinner=[[NSProgressIndicator alloc]initWithFrame:NSMakeRect(246,height-22,20,16)];
    action.spinner.style=NSProgressIndicatorStyleSpinning;
    action.spinner.controlSize=NSControlSizeSmall;
    [panel addSubview:action.spinner];

    action.generateButton=[NSButton buttonWithTitle:context.text(@"生成")
        target:action action:@selector(run:)];
    action.generateButton.frame=NSMakeRect(0,height-78,129,30);
    [panel addSubview:action.generateButton];
    action.stopButton=[NSButton buttonWithTitle:context.text(@"停止")
        target:action action:@selector(cancel:)];
    action.stopButton.frame=NSMakeRect(141,height-78,129,30);
    [panel addSubview:action.stopButton];

    CGFloat y=height-78-38;
    if(scoreLoop) {
        action.feedbackButton=[NSButton buttonWithTitle:context.text(@"按乐谱重新生成")
            target:action action:@selector(runFromScore:)];
        action.feedbackButton.frame=NSMakeRect(0,y-30,270,28);
        [panel addSubview:action.feedbackButton];
        y-=40;
        if(spec.importScore) {
            NSButton *button=[NSButton buttonWithTitle:context.text(@"导入为 MIDI 剪辑")
                target:action action:@selector(importScore:)];
            button.frame=NSMakeRect(0,y-30,270,28);[panel addSubview:button];y-=40;
        }
    }

    NSTextField* takeLabel=[NSTextField labelWithString:context.text(@"生成记录")];
    takeLabel.font=[NSFont systemFontOfSize:12];
    takeLabel.frame=NSMakeRect(0,y-18,270,18);
    [panel addSubview:takeLabel];
    action.takePopup=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(0,y-46,270,26) pullsDown:NO];
    action.takePopup.target=action;action.takePopup.action=@selector(takeChanged:);
    [action.takePopup addItemWithTitle:context.text(@"选择记录…")];
    for(NSDictionary* take in tri::projectresources::collection(node,@"generation.takes")) {
        if(![take isKindOfClass:NSDictionary.class])continue;
        const double seconds=[take[@"metadata"][@"seconds"] doubleValue];
        NSString* date=[take[@"metadata"][@"date"] isKindOfClass:NSString.class]?take[@"metadata"][@"date"]:@"";
        [action.takePopup addItemWithTitle:[NSString stringWithFormat:@"%@ · %d:%02d · %@",
            [take[@"name"] isKindOfClass:NSString.class]?take[@"name"]:@"?",
            int(seconds/60),int(std::fmod(seconds,60)),date]];
    }
    [panel addSubview:action.takePopup];
    y-=52;

    NSTextField* auditionLabel=[NSTextField labelWithString:context.text(@"试听（独立播放，不影响图）")];
    auditionLabel.font=[NSFont systemFontOfSize:12];
    auditionLabel.frame=NSMakeRect(0,y-18,270,18);
    [panel addSubview:auditionLabel];

    action.playButton=[NSButton buttonWithTitle:context.text(@"播放")
        target:action action:@selector(play:)];
    action.playButton.frame=NSMakeRect(0,y-50,84,30);
    [panel addSubview:action.playButton];
    action.pauseButton=[NSButton buttonWithTitle:context.text(@"暂停")
        target:action action:@selector(pause:)];
    action.pauseButton.frame=NSMakeRect(93,y-50,84,30);
    [panel addSubview:action.pauseButton];
    action.stopPlaybackButton=[NSButton buttonWithTitle:context.text(@"停止播放")
        target:action action:@selector(stopPlayback:)];
    action.stopPlaybackButton.frame=NSMakeRect(186,y-50,84,30);
    [panel addSubview:action.stopPlaybackButton];

    action.seek=[NSSlider sliderWithValue:0 minValue:0 maxValue:1 target:action
        action:@selector(seekChanged:)];
    action.seek.continuous=YES;
    action.seek.frame=NSMakeRect(0,y-74,198,20);
    [panel addSubview:action.seek];
    action.timeField=[NSTextField labelWithString:@""];
    action.timeField.font=[NSFont monospacedDigitSystemFontOfSize:10 weight:NSFontWeightRegular];
    action.timeField.alignment=NSTextAlignmentRight;
    action.timeField.frame=NSMakeRect(200,y-74,70,18);
    [panel addSubview:action.timeField];

    NSTextField* hint=[NSTextField wrappingLabelWithString:
        context.text(@"试听不经过图；节点内播放由「触发」端口驱动，走音频节点输出。")];
    hint.font=[NSFont systemFontOfSize:10];
    hint.textColor=[NSColor secondaryLabelColor];
    hint.frame=NSMakeRect(0,0,270,30);
    [panel addSubview:hint];

    soundnet::TaskService::Snapshot snapshot;
    if(context.taskSnapshot&&context.taskSnapshot(snapshot)&&snapshot.active()) {
        [action startPolling];
        [action tick:nil];
    } else [action refresh];
    return panel;
}

} // namespace tri::musicgen
