#import "TRIHost.h"
#import "../ui/TriEditor.h"
#import "../ui/SNLocalization.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "../src/WorkflowAssets.h"
#include "../src/OfflineRender.hpp"
#include "../src/WavFile.hpp"
#include "../src/MidiFileWriter.hpp"
#include <map>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

@interface TernaryApp : NSObject <NSApplicationDelegate>
@end
@implementation TernaryApp {
    TRIHost *_host;
    NSWindow *_window;
    id _keyboard;
    std::map<unsigned short,int> _held;
    int _transpose;
    NSMenuItem *_recordItem, *_inputRecordItem;
    NSURL *_recordURL, *_inputRecordURL;
}
- (void)applicationDidFinishLaunching:(NSNotification *)note {
    _host = [TRIHost new];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(languageChanged:)
        name:SNLanguageDidChangeNotification object:nil];
    [self buildMenu];
    TRIHost *host = _host;
    NSView *editor = TRICreateEditor(
        ^float(int p) { return [host value:p]; },
        ^(int p,float v) { [host setValue:v forParam:p]; },
        ^(unsigned char s,unsigned char a,unsigned char b){ [host sendStatus:s data1:a data2:b]; },
        ^BOOL{ return host.isInputEnabled; },
        ^(BOOL v){ host.inputEnabled=v; },
        ^NSString *{ return host.status; },
        ^NSDictionary *{ return host.workflowAssets; },
        ^BOOL(NSDictionary *assets){ return [host replaceWorkflowAssets:assets]; },
        ^BOOL{ return host.processingEnabled; },
        ^(BOOL enabled){ host.processingEnabled=enabled; },
        ^(uint64_t nodeID){ [host stopNode:nodeID]; });
    _window = [[NSWindow alloc] initWithContentRect:editor.frame
            styleMask:(NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable)
            backing:NSBackingStoreBuffered defer:NO];
    _window.title = @"Soundnet";
    _window.releasedWhenClosed = NO;
    _window.contentMinSize = NSMakeSize(1180, 620);
    _window.contentView = editor;
    _window.initialFirstResponder = editor;
    [_window center];
    [_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    __weak TernaryApp *weakSelf = self;
    _keyboard = [NSEvent addLocalMonitorForEventsMatchingMask:(NSEventMaskKeyDown|NSEventMaskKeyUp)
            handler:^NSEvent *(NSEvent *e){ return [weakSelf handle:e]; }];
    [host start];
}
- (void)languageChanged:(NSNotification *)notification { [self buildMenu]; }
- (void)useChinese:(id)sender { SNSetEnglish(NO); }
- (void)useEnglish:(id)sender { SNSetEnglish(YES); }
- (void)buildMenu {
    NSMenu *bar = [NSMenu new];
    NSMenuItem *appItem = [bar addItemWithTitle:@"Soundnet" action:nil keyEquivalent:@""];
    NSMenu *app = [[NSMenu alloc] initWithTitle:@"Soundnet"];
    appItem.submenu = app;
    [app addItemWithTitle:SNLocalized(@"关于 Soundnet") action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    NSMenuItem *languageItem=[app addItemWithTitle:SNLocalized(@"语言") action:nil keyEquivalent:@""];
    NSMenu *language=[[NSMenu alloc] initWithTitle:SNLocalized(@"语言")];
    languageItem.submenu=language;
    NSMenuItem *chinese=[language addItemWithTitle:@"中文" action:@selector(useChinese:) keyEquivalent:@""];
    chinese.target=self;chinese.state=SNIsEnglish()?NSControlStateValueOff:NSControlStateValueOn;
    NSMenuItem *english=[language addItemWithTitle:@"English" action:@selector(useEnglish:) keyEquivalent:@""];
    english.target=self;english.state=SNIsEnglish()?NSControlStateValueOn:NSControlStateValueOff;
    [app addItem:[NSMenuItem separatorItem]];
    [app addItemWithTitle:SNLocalized(@"隐藏 Soundnet") action:@selector(hide:) keyEquivalent:@"h"];
    [app addItemWithTitle:SNLocalized(@"退出 Soundnet") action:@selector(terminate:) keyEquivalent:@"q"];
    NSMenuItem *fileItem=[bar addItemWithTitle:SNLocalized(@"文件") action:nil keyEquivalent:@""];
    NSMenu *file=[[NSMenu alloc] initWithTitle:SNLocalized(@"文件")];
    fileItem.submenu=file;
    [file addItemWithTitle:SNLocalized(@"打开…") action:@selector(open:) keyEquivalent:@"o"];
    [file addItemWithTitle:SNLocalized(@"恢复自动保存…") action:@selector(restoreAutosave:) keyEquivalent:@""];
    [file addItemWithTitle:SNLocalized(@"保存…") action:@selector(save:) keyEquivalent:@"s"];
    [file addItem:[NSMenuItem separatorItem]];
    [file addItemWithTitle:SNLocalized(@"导出音频…") action:@selector(exportWav:) keyEquivalent:@""];
    [file addItemWithTitle:SNLocalized(@"导出 MIDI…") action:@selector(exportMidi:) keyEquivalent:@""];
    [file addItemWithTitle:SNLocalized(@"导出分轨…") action:@selector(exportStems:) keyEquivalent:@""];
    [file addItem:[NSMenuItem separatorItem]];
    _recordItem = [file addItemWithTitle:SNLocalized(@"录制输出…") action:@selector(toggleRecording:) keyEquivalent:@""];
    _recordItem.target = self;
    if(_host.isRecording)_recordItem.title=SNLocalized(@"停止录制");
    _inputRecordItem = [file addItemWithTitle:SNLocalized(@"录制输入…") action:@selector(toggleInputRecording:) keyEquivalent:@""];
    _inputRecordItem.target = self;
    if(_host.isInputRecording)_inputRecordItem.title=SNLocalized(@"停止录制输入");
    NSMenuItem *editItem=[bar addItemWithTitle:SNLocalized(@"编辑") action:nil keyEquivalent:@""];
    NSMenu *edit=[[NSMenu alloc] initWithTitle:SNLocalized(@"编辑")];
    editItem.submenu=edit;
    [edit addItemWithTitle:SNLocalized(@"撤销") action:@selector(undo:) keyEquivalent:@"z"];
    [edit addItemWithTitle:SNLocalized(@"重做") action:@selector(redo:) keyEquivalent:@"Z"];
    [edit addItem:[NSMenuItem separatorItem]];
    [edit addItemWithTitle:SNLocalized(@"剪切") action:@selector(cut:) keyEquivalent:@"x"];
    [edit addItemWithTitle:SNLocalized(@"复制") action:@selector(copy:) keyEquivalent:@"c"];
    [edit addItemWithTitle:SNLocalized(@"粘贴") action:@selector(paste:) keyEquivalent:@"v"];
    [edit addItemWithTitle:SNLocalized(@"全选") action:@selector(selectAll:) keyEquivalent:@"a"];
    NSMenuItem *audioItem = [bar addItemWithTitle:SNLocalized(@"音频") action:nil keyEquivalent:@""];
    NSMenu *audio = [[NSMenu alloc] initWithTitle:SNLocalized(@"音频")];
    audioItem.submenu = audio;
    [audio addItemWithTitle:SNLocalized(@"播放图形") action:@selector(playGraph:) keyEquivalent:@""];
    [audio addItemWithTitle:SNLocalized(@"停止图形") action:@selector(stopGraph:) keyEquivalent:@""];
    [audio addItem:[NSMenuItem separatorItem]];
    NSMenuItem *panic = [audio addItemWithTitle:SNLocalized(@"停止所有声音") action:@selector(panic:) keyEquivalent:@"."];
    panic.target = self;
    NSMenuItem *restart = [audio addItemWithTitle:SNLocalized(@"重启音频引擎") action:@selector(restart:) keyEquivalent:@""];
    restart.target = self;
    NSMenuItem *helpItem = [bar addItemWithTitle:SNLocalized(@"帮助") action:nil keyEquivalent:@""];
    NSMenu *help = [[NSMenu alloc] initWithTitle:SNLocalized(@"帮助")];
    helpItem.submenu = help;
    [help addItemWithTitle:SNLocalized(@"键盘与音频输入") action:@selector(help:) keyEquivalent:@"?"].target = self;
    [help addItemWithTitle:SNLocalized(@"快捷键…") action:@selector(showShortcuts:) keyEquivalent:@""];
    NSApp.mainMenu = bar;
}
- (void)panic:(id)sender {
    _held.clear();
    [NSApp sendAction:@selector(stopGraph:) to:_window.contentView from:sender];
}
- (void)toggleRecording:(id)sender {
    if ([_host isRecording]) {
        NSError *error = nil;
        if (![_host finishRecording:&error]) {
            NSAlert *alert = [NSAlert new];
            alert.messageText = SNLocalized(@"录制失败");
            alert.informativeText = error.localizedDescription ?: SNLocalized(@"无法写入 WAV 文件。");
            [alert beginSheetModalForWindow:_window completionHandler:nil];
            _recordItem.title = SNLocalized(@"录制输出…");
            return;
        }
        NSString *note = _host.recordingOverflow ? SNLocalized(@"已达到 60 秒上限，末尾被截断。") : SNLocalized(@"已保存为 16-bit 立体声 WAV。");
        NSAlert *alert = [NSAlert new];
        alert.messageText = SNLocalized(@"录制完成");
        alert.informativeText = [NSString stringWithFormat:@"%@\n%@", _recordURL.path ?: @"", note];
        [alert beginSheetModalForWindow:_window completionHandler:nil];
        _recordItem.title = SNLocalized(@"录制输出…");
        _recordURL = nil;
        return;
    }
    NSSavePanel *panel = [NSSavePanel savePanel];
    panel.title = SNLocalized(@"录制 Soundnet 输出");
    panel.nameFieldStringValue = SNLocalized(@"Soundnet 录制.wav");
    panel.allowedContentTypes = @[UTTypeWAV];
    [panel beginSheetModalForWindow:_window completionHandler:^(NSModalResponse response) {
        if (response != NSModalResponseOK || !panel.URL) return;
        NSError *error = nil;
        if (![self->_host startRecordingToURL:panel.URL error:&error]) {
            NSAlert *alert = [NSAlert new];
            alert.messageText = SNLocalized(@"无法开始录制");
            alert.informativeText = error.localizedDescription ?: SNLocalized(@"音频引擎尚未就绪。");
            [alert beginSheetModalForWindow:self->_window completionHandler:nil];
            return;
        }
        self->_recordURL = panel.URL;
        self->_recordItem.title = SNLocalized(@"停止录制");
    }];
}
- (void)toggleInputRecording:(id)sender {
    if ([_host isInputRecording]) {
        NSError *error = nil;
        if (![_host finishInputRecording:&error]) {
            NSAlert *alert = [NSAlert new];
            alert.messageText = SNLocalized(@"输入录制失败");
            alert.informativeText = error.localizedDescription ?: SNLocalized(@"无法写入 WAV 文件。");
            [alert beginSheetModalForWindow:_window completionHandler:nil];
            _inputRecordItem.title = SNLocalized(@"录制输入…");
            return;
        }
        NSString *note = _host.inputRecordingOverflow ? SNLocalized(@"已达到 60 秒上限，末尾被截断。") : SNLocalized(@"已保存为 16-bit 立体声 WAV。");
        NSAlert *alert = [NSAlert new];
        alert.messageText = SNLocalized(@"输入录制完成");
        alert.informativeText = [NSString stringWithFormat:@"%@\n%@", _inputRecordURL.path ?: @"", note];
        [alert beginSheetModalForWindow:_window completionHandler:nil];
        _inputRecordItem.title = SNLocalized(@"录制输入…");
        _inputRecordURL = nil;
        return;
    }
    NSSavePanel *panel = [NSSavePanel savePanel];
    panel.title = SNLocalized(@"录制 Soundnet 输入");
    panel.nameFieldStringValue = SNLocalized(@"Soundnet 输入录音.wav");
    panel.allowedContentTypes = @[UTTypeWAV];
    [panel beginSheetModalForWindow:_window completionHandler:^(NSModalResponse response) {
        if (response != NSModalResponseOK || !panel.URL) return;
        NSError *error = nil;
        if (![self->_host startInputRecordingToURL:panel.URL error:&error]) {
            NSAlert *alert = [NSAlert new];
            alert.messageText = SNLocalized(@"无法开始输入录制");
            alert.informativeText = error.localizedDescription ?: SNLocalized(@"请先启用音频输入。");
            [alert beginSheetModalForWindow:self->_window completionHandler:nil];
            return;
        }
        self->_inputRecordURL = panel.URL;
        self->_inputRecordItem.title = SNLocalized(@"停止录制输入");
    }];
}
- (void)restart:(id)sender { _held.clear(); [_host restartAudio]; }
- (void)help:(id)sender {
    NSAlert *alert = [NSAlert new];
    alert.messageText = SNLocalized(@"演奏 Soundnet");
    alert.informativeText = SNLocalized(@"A W S E D F T G Y H U J K 从 C4 演奏至 C5。Z / X 下移或上移八度。⌘. 停止所有声音。\n\n外接键盘接入本地 MIDI 节点。DAW 可把音轨和 MIDI 时钟发送至「Soundnet MIDI In」，接入 DAW MIDI 节点。独立版输出到系统所选声卡；在 Logic 的软件乐器轨道中加载 Soundnet AU，可将音频送回该轨道。\n\n音频输入来自系统默认输入设备。为避免啸叫，可使用耳机监听，或把输入切到声卡线路输入。\n\n「文件 → 录制输出」会把当前听到的最终输出录成 16-bit 立体声 WAV，最长 60 秒；「录制输入」会录下硬件输入，便于把外部声音做成采样；两者停止后都可再导入采样器或音频片段。");
    [alert beginSheetModalForWindow:_window completionHandler:nil];
}
- (NSEvent *)handle:(NSEvent *)e {
    // Key codes follow physical piano positions on all keyboard layouts.
    static const std::map<unsigned short,int> offsets = {
        {0,0},{13,1},{1,2},{14,3},{2,4},{3,5},{17,6},{5,7},{16,8},{4,9},{32,10},{38,11},{40,12}
    };
    unsigned short key = e.keyCode;
    if (e.type == NSEventTypeKeyUp) {
        auto it = _held.find(key);
        if (it == _held.end()) return e;
        [_host sendStatus:0x80 data1:it->second data2:0];
        _held.erase(it);
        return nil;
    }
    if (!_window.isKeyWindow || _window.attachedSheet ||
        (e.modifierFlags & (NSEventModifierFlagCommand|NSEventModifierFlagControl|NSEventModifierFlagOption)) ||
        [_window.firstResponder conformsToProtocol:@protocol(NSTextInputClient)] ||
        [_window.firstResponder isKindOfClass:NSTextField.class]) return e;
    if (key == 6 || key == 7) {
        if (!e.isARepeat) {
            _transpose = std::clamp(_transpose+(key==6?-12:12),-48,48);
            _window.title = _transpose ? [NSString stringWithFormat:@"Soundnet  ·  C%d",4+_transpose/12] : @"Soundnet";
        }
        return nil;
    }
    auto it = offsets.find(key);
    if (it == offsets.end()) return e;
    if (!e.isARepeat && !_held.count(key)) {
        int note = 60 + it->second + _transpose;
        _held.emplace(key,note);
        [_host sendStatus:0x90 data1:note data2:100];
    }
    return nil;
}
- (void)applicationWillResignActive:(NSNotification *)note {
    for (const auto &held: _held) [_host sendStatus:0x80 data1:held.second data2:0];
    _held.clear();
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender { return YES; }
- (void)applicationWillTerminate:(NSNotification *)note {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    if (_keyboard) [NSEvent removeMonitor:_keyboard];
    [_host savePatch];
    [_host stop];
}
@end

static int runExport(const char *input,const char *output,double seconds,int rate,int bits) {
    @autoreleasepool {
        NSData *data=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:input]];
        if(!data) { std::fprintf(stderr,"Soundnet: cannot read %s\n",input); return 1; }
        NSError *error=nil;
        id object=[NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
        if(![object isKindOfClass:NSDictionary.class]) {
            std::fprintf(stderr,"Soundnet: invalid workflow: %s\n",error.localizedDescription.UTF8String ?: "not JSON");
            return 1;
        }
        tri::GraphSpec graph;NSString *why=nil;
        if(!triassets::decodeGraph(object,graph,&why)) {
            std::fprintf(stderr,"Soundnet: %s\n",why.UTF8String ?: "invalid workflow");
            return 1;
        }
        tri::offline::Result rendered;std::string failure;
        if(!tri::offline::render(graph,seconds,double(rate),rendered,failure)) {
            std::fprintf(stderr,"Soundnet: %s\n",failure.c_str());return 1;
        }
        if(!tri::wav::write(output,rendered.left,rendered.right,rendered.rate,failure,bits)) {
            std::fprintf(stderr,"Soundnet: %s\n",failure.c_str());return 1;
        }
        const double peak=rendered.peak>0 ? 20*std::log10(rendered.peak) : -120;
        std::printf("PASS: exported %.1f s at %.1f kHz / %d-bit to %s (peak %.1f dBFS)\n",seconds,rate/1000.,bits,output,peak);
    }
    return 0;
}

static int runExportMidi(const char *input,const char *output,double seconds) {
    @autoreleasepool {
        NSData *data=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:input]];
        if(!data) { std::fprintf(stderr,"Soundnet: cannot read %s\n",input); return 1; }
        NSError *error=nil;
        id object=[NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
        if(![object isKindOfClass:NSDictionary.class]) {
            std::fprintf(stderr,"Soundnet: invalid workflow: %s\n",error.localizedDescription.UTF8String ?: "not JSON");
            return 1;
        }
        tri::GraphSpec graph;NSString *why=nil;
        if(!triassets::decodeGraph(object,graph,&why)) {
            std::fprintf(stderr,"Soundnet: %s\n",why.UTF8String ?: "invalid workflow");
            return 1;
        }
        tri::offline::Result rendered;std::string failure;
        if(!tri::offline::render(graph,seconds,48000,rendered,failure)) {
            std::fprintf(stderr,"Soundnet: %s\n",failure.c_str());return 1;
        }
        std::vector<tri::smf::TimedEvent> events;
        events.reserve(rendered.midi.size());
        for(const auto& e:rendered.midi)events.push_back({e.seconds,e.status,e.a,e.b});
        if(!tri::smf::writeMulti(output,events,seconds,rendered.bpm,failure)) {
            std::fprintf(stderr,"Soundnet: %s\n",failure.c_str());return 1;
        }
        std::printf("PASS: exported %zu MIDI events over %.1f s to %s\n",events.size(),seconds,output);
    }
    return 0;
}


static int runExportStems(const char *input,const char *directory,double seconds,int rate,int bits) {
    @autoreleasepool {
        NSData *data=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:input]];
        if(!data) { std::fprintf(stderr,"Soundnet: cannot read %s\n",input); return 1; }
        NSError *error=nil;
        id object=[NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
        if(![object isKindOfClass:NSDictionary.class]) {
            std::fprintf(stderr,"Soundnet: invalid workflow: %s\n",error.localizedDescription.UTF8String ?: "not JSON");
            return 1;
        }
        tri::GraphSpec graph;NSString *why=nil;
        if(!triassets::decodeGraph(object,graph,&why)) {
            std::fprintf(stderr,"Soundnet: %s\n",why.UTF8String ?: "invalid workflow");
            return 1;
        }
        std::vector<tri::offline::Stem> stems;std::string failure;
        if(!tri::offline::renderStems(graph,seconds,double(rate),stems,failure)) {
            std::fprintf(stderr,"Soundnet: %s\n",failure.c_str());return 1;
        }
        NSString *folder=[NSString stringWithUTF8String:directory];
        if(![[NSFileManager defaultManager] createDirectoryAtPath:folder
                withIntermediateDirectories:YES attributes:nil error:&error]) {
            std::fprintf(stderr,"Soundnet: cannot create %s: %s\n",directory,error.localizedDescription.UTF8String ?: "unknown error");
            return 1;
        }
        for(const auto& stem:stems) {
            NSString *name=[NSString stringWithFormat:@"stem-%llu.wav",(unsigned long long)stem.id];
            NSString *path=[folder stringByAppendingPathComponent:name];
            if(!tri::wav::write(path.UTF8String,stem.result.left,stem.result.right,stem.result.rate,failure,bits)) {
                std::fprintf(stderr,"Soundnet: %s\n",failure.c_str());return 1;
            }
            const double peak=stem.result.peak>0 ? 20*std::log10(stem.result.peak) : -120;
            std::printf("  %s: %s (peak %.1f dBFS)\n",stem.name.c_str(),name.UTF8String,peak);
        }
        std::printf("PASS: exported %zu audio stems over %.1f s to %s\n",stems.size(),seconds,directory);
    }
    return 0;
}


static int runValidate(const char *input) {
    @autoreleasepool {
        NSData *data=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:input]];
        if(!data) { std::fprintf(stderr,"Soundnet: cannot read %s\n",input); return 1; }
        NSError *error=nil;
        id object=[NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
        if(![object isKindOfClass:NSDictionary.class]) {
            std::fprintf(stderr,"Soundnet: invalid workflow: %s\n",error.localizedDescription.UTF8String ?: "not JSON");
            return 1;
        }
        tri::GraphSpec graph;NSString *why=nil;
        std::vector<tri::Diagnostic> diagnostics;
        if(!triassets::decodeGraph(object,graph,&why,false,false,&diagnostics)) {
            std::fprintf(stderr,"Soundnet: %s\n",why.UTF8String ?: "invalid workflow");
            return 1;
        }
        for(const auto& diagnostic:diagnostics)
            std::fprintf(stderr,"Soundnet: warning: node %llu: %s\n",
                         (unsigned long long)diagnostic.node,diagnostic.message.c_str());
        std::printf("OK: %zu nodes, %zu edges\n",graph.nodes.size(),graph.edges.size());
    }
    return 0;
}

int main(int argc, const char **argv) {
    @autoreleasepool {
        if (argc==2 && strcmp(argv[1],"--self-test")==0) return [TRIHost runOfflineCheck];
        if (argc==2 && (strcmp(argv[1],"--version")==0 || strcmp(argv[1],"-v")==0)) {
            NSDictionary *info=[NSBundle mainBundle].infoDictionary;
            std::printf("Soundnet %s (%s)\n",[info[@"CFBundleShortVersionString"] UTF8String] ?: "2.37.0",
                        [info[@"CFBundleVersion"] UTF8String] ?: "43");
            return 0;
        }
        if (argc==3 && strcmp(argv[1],"--validate")==0) return runValidate(argv[2]);
        if (argc>=4 && argc<=7 && strcmp(argv[1],"--export")==0) {
            const double seconds=argc>=5 ? std::atof(argv[4]) : 8.0;
            const int rate=argc>=6 ? std::atoi(argv[5]) : 48000;
            const int bits=argc>=7 ? std::atoi(argv[6]) : 16;
            return runExport(argv[2],argv[3],seconds,rate,bits);
        }
        if ((argc==4 || argc==5) && strcmp(argv[1],"--export-midi")==0) {
            const double seconds=argc==5 ? std::atof(argv[4]) : 8.0;
            return runExportMidi(argv[2],argv[3],seconds);
        }
        if (argc>=4 && argc<=7 && strcmp(argv[1],"--export-stems")==0) {
            const double seconds=argc>=5 ? std::atof(argv[4]) : 8.0;
            const int rate=argc>=6 ? std::atoi(argv[5]) : 48000;
            const int bits=argc>=7 ? std::atoi(argv[6]) : 16;
            return runExportStems(argv[2],argv[3],seconds,rate,bits);
        }
        if (argc==2 && strcmp(argv[1],"--help")==0) {
            std::printf("Usage: Soundnet [--self-test | --version | --validate workflow.json | --export workflow.json output.wav [seconds] [rate] [bits] | --export-midi workflow.json output.mid [seconds] | --export-stems workflow.json directory [seconds] [rate] [bits]]\n");
            return 0;
        }
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        __attribute__((objc_precise_lifetime)) TernaryApp *delegate = [TernaryApp new];
        app.delegate = delegate;
        [app run];
        (void)delegate; // Keep alive while NSApplication (weakly) references the delegate.
    }
    return 0;
}
