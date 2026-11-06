#import "TRIHost.h"
#import "AudioBridge.hpp"
#include "PatchRouting.hpp"
#import <AVFoundation/AVFoundation.h>
#import <CoreMIDI/CoreMIDI.h>
#import <mach/mach_time.h>
#include <set>
#include <memory>

static void receiveUMP(void *context, MIDITimeStamp time, MIDIUniversalMessage message) {
    if (message.type != kMIDIMessageTypeChannelVoice1) return;
    auto &v = message.channelVoice1;
    const auto status = uint8_t((v.status << 4) | v.channel);
    uint8_t a = 0, b = 0;
    switch (v.status) {
        case kMIDICVStatusNoteOn:
        case kMIDICVStatusNoteOff: a=v.note.number; b=v.note.velocity; break;
        case kMIDICVStatusControlChange: a=v.controlChange.index; b=v.controlChange.data; break;
        case kMIDICVStatusPitchBend: a=v.pitchBend & 127; b=(v.pitchBend >> 7) & 127; break;
        default: return;
    }
    static_cast<TRIRealtime *>(context)->enqueue(status,a,b,time);
}

static NSError *audioError(NSString *description) {
    return [NSError errorWithDomain:@"Ternary.Audio" code:1
                          userInfo:@{NSLocalizedDescriptionKey: description}];
}
static void displayError(NSString *title, NSString *reason) {
    NSAlert *alert = [NSAlert new];
    alert.messageText = title;
    alert.informativeText = reason;
    if (NSApp.keyWindow) [alert beginSheetModalForWindow:NSApp.keyWindow completionHandler:nil];
    else [alert runModal];
}
static AVAudioSourceNode *sourceNode(TRIRealtime *rt, AVAudioFormat *format, BOOL input) {
    return [[AVAudioSourceNode alloc] initWithFormat:format renderBlock:^OSStatus(
            BOOL *silence, const AudioTimeStamp *stamp, AVAudioFrameCount frames, AudioBufferList *out) {
        if (out->mNumberBuffers != 2 || !out->mBuffers[0].mData || !out->mBuffers[1].mData)
            return kAudio_ParamError;
        *silence = NO;
        rt->render((float *)out->mBuffers[0].mData, (float *)out->mBuffers[1].mData,
                   (int)frames, input, stamp->mFlags & kAudioTimeStampHostTimeValid ? stamp->mHostTime : 0);
        return noErr;
    }];
}

@implementation TRIHost {
    std::unique_ptr<TRIRealtime> _rt;
    tri::layout::State _patchLayout;
    tri::RouteEdit _routeEdit;
    AVAudioEngine *_graph;
    MIDIClientRef _client;
    MIDIPortRef _port;
    MIDIEndpointRef _destination;
    std::set<MIDIEndpointRef> _sources;
    BOOL _inputEnabled, _waitingForInput, _closed, _midiReady;
    double _sampleRate;
    NSString *_error;
    id _configObserver;
    NSUInteger _generation;
}
- (instancetype)init {
    if ((self = [super init])) {
        _rt = std::make_unique<TRIRealtime>();
        NSUserDefaults *defaults=[NSUserDefaults standardUserDefaults];
        NSArray *saved=[defaults arrayForKey:@"Ternary.Patch.1"];
        // Older versions stored only the original controls; they use the default patch cables.
        NSUInteger controls=MIN(saved.count,static_cast<NSUInteger>(tri::RouteArpIn));
        for (NSUInteger i=0; i<controls; ++i)
            if ([saved[i] isKindOfClass:NSNumber.class]) _rt->dsp.set(static_cast<int>(i),[saved[i] floatValue]);
        for(NSUInteger i=tri::ChordRoot;i<MIN(saved.count,static_cast<NSUInteger>(tri::ParamCount));++i)
            if([saved[i] isKindOfClass:NSNumber.class])_rt->dsp.set((int)i,[saved[i] floatValue]);
        if (saved.count>tri::RouteOutputIn) {
            auto graph=tri::defaultRouting;
            bool complete=true;
            for (int id=tri::RouteArpIn; id<=tri::RouteVelocityClock && id<(int)saved.count; ++id) {
                if (![saved[id] isKindOfClass:NSNumber.class]) { complete=false; break; }
                double source=[saved[id] doubleValue];
                if (!std::isfinite(source) || source<0 || source>14 || source!=std::trunc(source)) { complete=false; break; }
                graph[id-tri::RouteArpIn]=static_cast<int>(source);
            }
            if (complete && tri::validRouting(graph)) _rt->dsp.setRouting(graph);
        }
        _patchLayout=tri::layout::defaults();
        NSArray *layout=[defaults arrayForKey:@"Ternary.NodeLayout.1"];
        for (NSUInteger i=0; i<MIN(layout.count,_patchLayout.size()); ++i) {
            if (![layout[i] isKindOfClass:NSNumber.class]) continue;
            float value=[layout[i] floatValue];
            if (tri::layout::valid(tri::layout::first+static_cast<int>(i),value)) _patchLayout[i]=value;
        }
    }
    return self;
}
- (void)start {
    _closed = NO;
    [self setupMIDI];
    [self restartAudio];
}
- (void)setupMIDI {
    __weak TRIHost *weakSelf = self;
    OSStatus err = MIDIClientCreateWithBlock(CFSTR("Soundnet"), &_client, ^(const MIDINotification *n){
        if (n->messageID == kMIDIMsgSetupChanged || n->messageID == kMIDIMsgObjectAdded ||
            n->messageID == kMIDIMsgObjectRemoved)
            dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf refreshSources]; });
    });
    if (err) return;
    TRIRealtime *rt = _rt.get();
    MIDIReceiveBlock receive = ^(const MIDIEventList *list, void *) {
        MIDIEventListForEachEvent(list, receiveUMP, rt);
    };
    _midiReady = !MIDIInputPortCreateWithProtocol(_client,CFSTR("Soundnet Input"),
                                                   kMIDIProtocol_1_0,&_port,receive);
    MIDIDestinationCreateWithProtocol(_client,CFSTR("Soundnet MIDI In"),kMIDIProtocol_1_0,&_destination,receive);
    [self refreshSources];
}
- (void)refreshSources {
    if (!_port || _closed) return;
    std::set<MIDIEndpointRef> live;
    for (ItemCount i=0; i<MIDIGetNumberOfSources(); ++i) {
        MIDIEndpointRef src = MIDIGetSource(i);
        if (!src) continue;
        if (_sources.count(src) || MIDIPortConnectSource(_port,src,nullptr) == noErr) live.insert(src);
    }
    // CoreMIDI removes connections to retired endpoints automatically.
    for (auto old: _sources) if (!live.count(old)) { [self panic]; break; }
    _sources.swap(live);
}
- (void)tearDownGraph {
    ++_generation;
    if (_configObserver) [[NSNotificationCenter defaultCenter] removeObserver:_configObserver];
    _configObserver = nil;
    [_graph stop];
    _graph = nil;
    _rt->clearEvents();
}
- (NSError *)buildGraphWithInput:(BOOL)input {
    [self tearDownGraph];
    @try {
        _graph = [AVAudioEngine new];
        AVAudioFormat *device = [_graph.outputNode outputFormatForBus:0];
        if (!(device.sampleRate > 0) || !device.channelCount)
            return audioError(@"找不到输出设备。请连接声卡或在系统设置中选择声音输出，然后重启音频引擎。");
        _sampleRate = device.sampleRate;
        mach_timebase_info_data_t base;
        mach_timebase_info(&base);
        _rt->framesPerTick = _sampleRate * base.numer / base.denom * 1.e-9;
        double inputRate = _sampleRate;
        if (input) {
            AVAudioInputNode *mic = _graph.inputNode;
            AVAudioFormat *micFormat = [mic outputFormatForBus:0];
            if (micFormat.channelCount < 1 || !(micFormat.sampleRate > 0) ||
                micFormat.commonFormat != AVAudioPCMFormatFloat32)
                return audioError(@"输入设备未就绪。请在「系统设置 → 声音 → 输入」中选择麦克风或声卡。");
            inputRate = micFormat.sampleRate;
            TRIRealtime *rt = _rt.get();
            AVAudioSinkNode *sink = [[AVAudioSinkNode alloc] initWithReceiverBlock:^OSStatus(
                    const AudioTimeStamp *, AVAudioFrameCount frames, const AudioBufferList *in) {
                if (!in->mNumberBuffers || !in->mBuffers[0].mData) return noErr;
                const auto &first = in->mBuffers[0];
                const float *l = static_cast<float *>(first.mData);
                const float *r = first.mNumberChannels > 1 ? l+1 : l;
                if (first.mNumberChannels == 1 && in->mNumberBuffers > 1 && in->mBuffers[1].mData)
                    r = static_cast<float *>(in->mBuffers[1].mData);
                rt->input.append(l,r,std::max(1u,first.mNumberChannels),frames);
                return noErr;
            }];
            [_graph attachNode:sink];
            [_graph connect:mic to:sink format:micFormat];
        }
        _rt->input.prepare(inputRate,_sampleRate);
        _rt->dsp.prepare(_sampleRate);
        AVAudioFormat *format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:_sampleRate channels:2];
        AVAudioSourceNode *source = sourceNode(_rt.get(),format,input);
        [_graph attachNode:source];
        [_graph connect:source to:_graph.mainMixerNode format:format];
        NSError *err = nil;
        [_graph prepare];
        if (![_graph startAndReturnError:&err]) return err ?: audioError(@"音频引擎启动失败。");
        __weak TRIHost *weakSelf = self;
        NSUInteger generation = _generation;
        _configObserver = [[NSNotificationCenter defaultCenter]
            addObserverForName:AVAudioEngineConfigurationChangeNotification object:_graph queue:nil
            usingBlock:^(NSNotification *) {
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100*NSEC_PER_MSEC),dispatch_get_main_queue(), ^{
                    TRIHost *host = weakSelf;
                    if (host && !host->_closed && host->_generation == generation) [host restartAudio];
                });
            }];
        _error = nil;
        return nil;
    } @catch (NSException *e) {
        return audioError(e.reason ?: @"音频设备配置失败。");
    }
}
- (void)restartAudio {
    if (_closed) return;
    NSError *failure = [self buildGraphWithInput:_inputEnabled];
    if (failure && _inputEnabled) {
        _inputEnabled = NO;
        NSError *outputFailure = [self buildGraphWithInput:NO];
        if (outputFailure) { [self tearDownGraph]; _error = outputFailure.localizedDescription; }
        displayError(@"音频输入无法启用",failure.localizedDescription);
    } else if (failure) {
        [self tearDownGraph];
        _error = failure.localizedDescription;
        NSLog(@"Audio device: %@", _error);
    }
}
- (NSString *)status {
    if (_error) return @"音频离线 · 从「音频」菜单重试";
    if (_waitingForInput) return @"正在等待麦克风授权";
    NSString *midi = _midiReady ? [NSString stringWithFormat:@"MIDI × %zu",_sources.size()] : @"MIDI 离线";
    return [NSString stringWithFormat:@"%g kHz  ·  %@",_sampleRate/1000.,midi];
}
- (BOOL)isInputEnabled { return _inputEnabled; }
- (void)setInputEnabled:(BOOL)enabled {
    if (_closed || _waitingForInput || enabled == _inputEnabled) return;
    if (!enabled) { _inputEnabled = NO; [self restartAudio]; return; }
    AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    if (status == AVAuthorizationStatusNotDetermined) {
        _waitingForInput = YES;
        __weak TRIHost *weakSelf = self;
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
            dispatch_async(dispatch_get_main_queue(), ^{
                TRIHost *host = weakSelf;
                if (host && !host->_closed) {
                    host->_waitingForInput = NO;
                    [host setInputEnabled:YES];
                }
            });
        }];
        return;
    }
    if (status != AVAuthorizationStatusAuthorized) {
        displayError(@"需要麦克风权限",@"在「系统设置 → 隐私与安全性 → 麦克风」里允许 Soundnet，然后再次打开音频输入。");
        return;
    }
    _inputEnabled = YES;
    [self restartAudio];
}
- (float)value:(int)param {
    if (tri::layout::contains(param)) return _patchLayout[param-tri::layout::first];
    return _rt->dsp.get(param);
}
- (void)setValue:(float)value forParam:(int)param {
    if (tri::layout::contains(param)) {
        if (tri::layout::valid(param,value)) _patchLayout[param-tri::layout::first]=value;
        return;
    }
    if (_routeEdit.handle(param,value,[&] {return _rt->dsp.routing();},
                          [&](const tri::Routing& graph) { _rt->dsp.setRouting(graph); })) return;
    _rt->dsp.set(param,value);
}
- (void)sendStatus:(uint8_t)status data1:(uint8_t)a data2:(uint8_t)b { _rt->enqueue(status,a,b); }
- (void)panic { _rt->panicFlag.store(true); }
- (void)savePatch {
    NSMutableArray *values = [NSMutableArray array];
    for (int i=0; i<tri::ParamCount; ++i) [values addObject:@(_rt->dsp.get(i))];
    NSUserDefaults *defaults=[NSUserDefaults standardUserDefaults];
    [defaults setObject:values forKey:@"Ternary.Patch.1"];
    NSMutableArray *layout=[NSMutableArray arrayWithCapacity:_patchLayout.size()];
    for (float v:_patchLayout) [layout addObject:@(v)];
    [defaults setObject:layout forKey:@"Ternary.NodeLayout.1"];
}
- (void)stop {
    _closed = YES;
    if (_client) { MIDIClientDispose(_client); _client = 0; _port = 0; }
    [self tearDownGraph];
}
+ (int)runOfflineCheck {
    // Feed a real MIDI event list and an input impulse through the same graph callback the app uses.
    auto rt = std::make_unique<TRIRealtime>();
    rt->dsp.prepare(48000);
    rt->input.prepare(48000,48000);
    rt->dsp.set(tri::InputGain,1);
    AVAudioFormat *format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:48000 channels:2];
    AVAudioEngine *graph = [AVAudioEngine new];
    AVAudioSourceNode *source = sourceNode(rt.get(),format,YES);
    [graph attachNode:source];
    [graph connect:source to:graph.mainMixerNode format:format];
    NSError *error = nil;
    if (![graph enableManualRenderingMode:AVAudioEngineManualRenderingModeOffline
                                   format:format maximumFrameCount:512 error:&error] ||
        ![graph startAndReturnError:&error]) { fprintf(stderr,"%s\n",error.localizedDescription.UTF8String); return 1; }
    AVAudioPCMBuffer *buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:512];
    auto peak = [&]{
        if ([graph renderOffline:512 toBuffer:buffer error:&error] != AVAudioEngineManualRenderingStatusSuccess)
            return -1.f;
        float maximum = 0;
        for (int c=0; c<2; ++c) for (unsigned i=0; i<buffer.frameLength; ++i)
            maximum = std::max(maximum,std::abs(buffer.floatChannelData[c][i]));
        return maximum;
    };
    float impulse[1024]{};
    impulse[64] = .3f;
    rt->input.append(impulse,impulse,1,1024);
    float externalPeak = peak();
    MIDIEventList list{};
    auto *packet = MIDIEventListInit(&list,kMIDIProtocol_1_0);
    UInt32 word = MIDI1UPNoteOn(0,0,69,110);
    if (!MIDIEventListAdd(&list,sizeof(list),packet,0,1,&word)) return 2;
    MIDIEventListForEachEvent(&list,receiveUMP,rt.get());
    float notePeak = peak();
    int result = externalPeak>.001f && notePeak>.001f ? 0 : 3;
    fprintf(stderr,"Offline app graph: input peak %.5f; MIDI peak %.5f; %s\n",externalPeak,notePeak,result?"FAIL":"PASS");
    [graph stop];
    return result;
}
@end
