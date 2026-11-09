#import "../ui/SNLocalization.h"
#import "TRIHost.h"
#import "AudioBridge.hpp"
#include "PatchRouting.hpp"
#include "WorkflowAssetStore.hpp"
#include "../src/WavFile.hpp"
#import <AVFoundation/AVFoundation.h>
#import <CoreMIDI/CoreMIDI.h>
#import <mach/mach_time.h>
#include <set>
#include <memory>

struct MIDIInputContext {
    TRIRealtime *realtime = nullptr;
    bool daw = false;
};
struct MIDIOutputContext {
    TRIRealtime *realtime = nullptr;
    MIDIEndpointRef endpoint = 0;
};
static void sendUMP(void *context,int frame,uint8_t status,uint8_t a,uint8_t b) {
    const auto &output=*static_cast<MIDIOutputContext *>(context);
    const auto &rt=*output.realtime;
    if (!output.endpoint) return;
    MIDIEventList list{};
    list.protocol=kMIDIProtocol_1_0;
    list.numPackets=1;
    auto &packet=list.packet[0];
    if (rt.outputStart && rt.framesPerTick>0)
        packet.timeStamp=rt.outputStart+uint64_t((rt.outputOffset+frame)/rt.framesPerTick);
    packet.wordCount=1;
    packet.words[0]=(UInt32(kMIDIMessageTypeChannelVoice1)<<28) | (UInt32(status)<<16) | (UInt32(a)<<8) | b;
    MIDIReceivedEventList(output.endpoint,&list);
}
static void receiveUMP(void *context, MIDITimeStamp time, MIDIUniversalMessage message) {
    auto &input = *static_cast<MIDIInputContext *>(context);
    // CoreMIDI can pass realtime system messages as raw UMPs in the
    // visitor's unknown variant, even on a MIDI 1.0 endpoint.
    const bool rawSystem = message.type==kMIDIMessageTypeInvalid &&
                           (message.unknown.words[0]>>28)==kMIDIMessageTypeSystem;
    if (input.daw && (message.type==kMIDIMessageTypeSystem || rawSystem)) {
        UInt32 word=rawSystem ? message.unknown.words[0] : 0;
        uint8_t status = rawSystem ? uint8_t(word>>16) : uint8_t(message.system.status);
        if (status==kMIDIStatusTimingClock || status==kMIDIStatusStart ||
            status==kMIDIStatusContinue || status==kMIDIStatusStop || status==kMIDIStatusSongPosPointer) {
            UInt16 pos = 0;
            if (status==kMIDIStatusSongPosPointer)
                pos=rawSystem ? ((word>>8)&127) | ((word&127)<<7) : message.system.songPositionPointer;
            input.realtime->enqueue(status,pos & 127,(pos>>7)&127,time ? time : mach_absolute_time(),true);
        }
        return;
    }
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
    input.realtime->enqueue(status,a,b,time,input.daw);
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
    tri::WorkflowAssetStore _assets;
    MIDIInputContext _physicalInput, _dawInput;
    MIDIOutputContext _midiOutput;
    AVAudioEngine *_graph;
    MIDIClientRef _client;
    MIDIPortRef _port;
    MIDIEndpointRef _destination;
    std::set<MIDIEndpointRef> _sources;
    BOOL _inputEnabled, _waitingForInput, _closed, _midiReady;
    double _sampleRate;
    NSString *_error;
    NSURL *_recordingURL;
    NSURL *_inputRecordingURL;
    BOOL _recordOverflow, _inputRecordOverflow;
    id _configObserver;
    NSUInteger _generation;
}
- (instancetype)init {
    if ((self = [super init])) {
        _rt = std::make_unique<TRIRealtime>();
        NSUserDefaults *defaults=[NSUserDefaults standardUserDefaults];
        NSArray *saved=[defaults arrayForKey:@"Ternary.Patch.1"];
        // Restore routes as one graph. Earlier patch arrays end before the new
        // ports; those ports keep their default disconnected source.
        auto graph = tri::defaultRouting;
        bool validGraph = true;
        for (NSUInteger i=0; i<MIN(saved.count,static_cast<NSUInteger>(tri::ParamCount)); ++i) {
            id value = saved[i];
            if (tri::routeParam((int)i)) {
                if (![value isKindOfClass:NSNumber.class]) { validGraph=false; continue; }
                const double source = [value doubleValue];
                if (!std::isfinite(source) || source!=std::trunc(source) ||
                    source<0 || source>tri::layout::nodeCount) { validGraph=false; continue; }
                graph[tri::routeIndex((int)i)] = (int)source;
            } else if ([value isKindOfClass:NSNumber.class]) {
                _rt->dsp.set((int)i,[value floatValue]);
            }
        }
        if (validGraph && tri::validRouting(graph)) _rt->dsp.setRouting(graph);
        NSDictionary *assets = [defaults dictionaryForKey:@"Soundnet.WorkflowAssets.1"];
        if (assets) _assets.replace(assets,_rt->dsp);
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
    _midiOutput.realtime=_rt.get();
    if (!MIDISourceCreateWithProtocol(_client,CFSTR("Soundnet MIDI Out"),kMIDIProtocol_1_0,&_midiOutput.endpoint))
        _rt->dsp.setMidiOutput(&_midiOutput,sendUMP);
    _physicalInput = {_rt.get(),false};
    _dawInput = {_rt.get(),true};
    MIDIInputContext *physical = &_physicalInput, *daw = &_dawInput;
    MIDIReceiveBlock receive = ^(const MIDIEventList *list, void *) {
        MIDIEventListForEachEvent(list, receiveUMP, physical);
    };
    _midiReady = !MIDIInputPortCreateWithProtocol(_client,CFSTR("Soundnet Input"),
                                                   kMIDIProtocol_1_0,&_port,receive);
    MIDIDestinationCreateWithProtocol(_client,CFSTR("Soundnet MIDI In"),kMIDIProtocol_1_0,&_destination,
        ^(const MIDIEventList *list,void *) { MIDIEventListForEachEvent(list,receiveUMP,daw); });
    [self refreshSources];
}
- (void)refreshSources {
    if (!_port || _closed) return;
    std::set<MIDIEndpointRef> live;
    for (ItemCount i=0; i<MIDIGetNumberOfSources(); ++i) {
        MIDIEndpointRef src = MIDIGetSource(i);
        if (!src || src==_midiOutput.endpoint) continue;
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
            return audioError(SNLocalized(@"找不到输出设备。请连接声卡或在系统设置中选择声音输出，然后重启音频引擎。"));
        _sampleRate = device.sampleRate;
        mach_timebase_info_data_t base;
        mach_timebase_info(&base);
        _rt->sampleRate = _sampleRate;
        _rt->framesPerTick = _sampleRate * base.numer / base.denom * 1.e-9;
        double inputRate = _sampleRate;
        if (input) {
            AVAudioInputNode *mic = _graph.inputNode;
            AVAudioFormat *micFormat = [mic outputFormatForBus:0];
            if (micFormat.channelCount < 1 || !(micFormat.sampleRate > 0) ||
                micFormat.commonFormat != AVAudioPCMFormatFloat32)
                return audioError(SNLocalized(@"输入设备未就绪。请在「系统设置 → 声音 → 输入」中选择麦克风或声卡。"));
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
        if (![_graph startAndReturnError:&err]) return err ?: audioError(SNLocalized(@"音频引擎启动失败。"));
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
        return audioError(e.reason ?: SNLocalized(@"音频设备配置失败。"));
    }
}
- (void)restartAudio {
    if (_closed) return;
    NSError *failure = [self buildGraphWithInput:_inputEnabled];
    if (failure && _inputEnabled) {
        _inputEnabled = NO;
        NSError *outputFailure = [self buildGraphWithInput:NO];
        if (outputFailure) { [self tearDownGraph]; _error = outputFailure.localizedDescription; }
        displayError(SNLocalized(@"音频输入无法启用"),failure.localizedDescription);
    } else if (failure) {
        [self tearDownGraph];
        _error = failure.localizedDescription;
        NSLog(@"Audio device: %@", _error);
    }
}
- (NSString *)status {
    if (_recordingURL && _inputRecordingURL) return SNLocalized(@"正在录制输出与输入");
    if (_recordingURL) return _rt->recording.overflow.load() ? SNLocalized(@"录制中 · 已达 60 秒上限") : SNLocalized(@"录制中 · 再次选择「停止录制」保存");
    if (_inputRecordingURL) return _rt->inputRecording.overflow.load() ? SNLocalized(@"输入录制中 · 已达 60 秒上限") : SNLocalized(@"输入录制中 · 再次选择「停止录制输入」保存");
    if (_error) return SNLocalized(@"音频离线 · 从「音频」菜单重试");
    if (_waitingForInput) return SNLocalized(@"正在等待麦克风授权");
    NSString *midi = _midiReady ? [NSString stringWithFormat:@"MIDI × %zu",_sources.size()] : SNLocalized(@"MIDI 离线");
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
        displayError(SNLocalized(@"需要麦克风权限"),SNLocalized(@"在「系统设置 → 隐私与安全性 → 麦克风」里允许 Soundnet，然后再次打开音频输入。"));
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
- (BOOL)processingEnabled { return _rt->dsp.processingEnabled(); }
- (void)setProcessingEnabled:(BOOL)enabled { _rt->setProcessingEnabled(enabled); }
- (BOOL)isRecording { return _recordingURL != nil; }
- (BOOL)recordingOverflow { return _recordOverflow; }
- (BOOL)startRecordingToURL:(NSURL *)url error:(NSError **)error {
    if (_closed || !_rt || _sampleRate <= 0) {
        if (error) *error = audioError(SNLocalized(@"音频引擎尚未就绪。"));
        return NO;
    }
    if (_recordingURL || _inputRecordingURL) {
        if (error) *error = audioError(SNLocalized(@"已经有录音在进行。"));
        return NO;
    }
    _recordOverflow = NO;
    _rt->recording.start(_sampleRate, 60.0);
    _recordingURL = url;
    return YES;
}
- (BOOL)isInputRecording { return _inputRecordingURL != nil; }
- (BOOL)inputRecordingOverflow { return _inputRecordOverflow; }
- (BOOL)startInputRecordingToURL:(NSURL *)url error:(NSError **)error {
    if (_closed || !_rt || _sampleRate <= 0) {
        if (error) *error = audioError(SNLocalized(@"音频引擎尚未就绪。"));
        return NO;
    }
    if (!_inputEnabled) {
        if (error) *error = audioError(SNLocalized(@"请先在「音频」菜单启用音频输入。"));
        return NO;
    }
    if (_recordingURL || _inputRecordingURL) {
        if (error) *error = audioError(SNLocalized(@"已经有录音在进行。"));
        return NO;
    }
    _inputRecordOverflow = NO;
    _rt->inputRecording.start(_sampleRate, 60.0);
    _inputRecordingURL = url;
    return YES;
}
- (BOOL)finishInputRecording:(NSError **)error {
    if (!_inputRecordingURL) {
        if (error) *error = audioError(SNLocalized(@"当前没有输入录音。"));
        return NO;
    }
    _rt->inputRecording.stop();
    _inputRecordOverflow = _rt->inputRecording.overflow.load();
    const size_t frames = _rt->inputRecording.frames();
    const double rate = _rt->inputRecording.rate;
    NSString *path = _inputRecordingURL.path;
    _inputRecordingURL = nil;
    if (!frames) {
        if (error) *error = audioError(SNLocalized(@"没有录到音频。"));
        return NO;
    }
    std::vector<float> left(_rt->inputRecording.left.begin(), _rt->inputRecording.left.begin() + frames);
    std::vector<float> right(_rt->inputRecording.right.begin(), _rt->inputRecording.right.begin() + frames);
    std::string why;
    if (!path.length || !tri::wav::write(path.UTF8String, left, right, rate, why)) {
        if (error) *error = audioError([NSString stringWithUTF8String:why.c_str()]);
        return NO;
    }
    return YES;
}
- (BOOL)finishRecording:(NSError **)error {
    if (!_recordingURL) {
        if (error) *error = audioError(SNLocalized(@"当前没有录音。"));
        return NO;
    }
    _rt->recording.stop();
    _recordOverflow = _rt->recording.overflow.load();
    const size_t frames = _rt->recording.frames();
    const double rate = _rt->recording.rate;
    NSString *path = _recordingURL.path;
    _recordingURL = nil;
    if (!frames) {
        if (error) *error = audioError(SNLocalized(@"没有录到音频。"));
        return NO;
    }
    std::vector<float> left(_rt->recording.left.begin(), _rt->recording.left.begin() + frames);
    std::vector<float> right(_rt->recording.right.begin(), _rt->recording.right.begin() + frames);
    std::string why;
    if (!path.length || !tri::wav::write(path.UTF8String, left, right, rate, why)) {
        if (error) *error = audioError([NSString stringWithUTF8String:why.c_str()]);
        return NO;
    }
    return YES;
}
- (NSDictionary *)workflowAssets { return _assets.current; }
- (BOOL)replaceWorkflowAssets:(NSDictionary *)assets { return _assets.replace(assets,_rt->dsp); }
// Node-level stop: the graph applies it at the next block boundary, so this is
// safe to call from the UI thread.
- (void)stopNode:(uint64_t)nodeID { _rt->dsp.stopNode(nodeID); }
- (void)savePatch {
    NSMutableArray *values = [NSMutableArray array];
    for (int i=0; i<tri::ParamCount; ++i) [values addObject:@(_rt->dsp.get(i))];
    NSUserDefaults *defaults=[NSUserDefaults standardUserDefaults];
    [defaults setObject:values forKey:@"Ternary.Patch.1"];
    [defaults setObject:_assets.current forKey:@"Soundnet.WorkflowAssets.1"];
    NSMutableArray *layout=[NSMutableArray arrayWithCapacity:_patchLayout.size()];
    for (float v:_patchLayout) [layout addObject:@(v)];
    [defaults setObject:layout forKey:@"Ternary.NodeLayout.1"];
}
- (void)stop {
    _closed = YES;
    if (_recordingURL) { _rt->recording.stop(); _recordingURL = nil; }
    if (_inputRecordingURL) { _rt->inputRecording.stop(); _inputRecordingURL = nil; }
    // Stop rendering and release downstream notes while the virtual source is live.
    [self tearDownGraph];
    _rt->dsp.setMidiOutput(nullptr,nullptr);
    if (_client) { MIDIClientDispose(_client); _client = 0; _port = 0; }
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
    MIDIInputContext physical{rt.get(),false};
    MIDIEventListForEachEvent(&list,receiveUMP,&physical);
    float notePeak = peak();
    int result = externalPeak>.001f && notePeak>.001f ? 0 : 3;
    fprintf(stderr,"Offline app graph: input peak %.5f; MIDI peak %.5f; %s\n",externalPeak,notePeak,result?"FAIL":"PASS");
    [graph stop];
    return result;
}
@end
