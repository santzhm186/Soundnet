#import "TRIHost.h"
#import "../ui/TriEditor.h"
#include <map>
#include <algorithm>

@interface TernaryApp : NSObject <NSApplicationDelegate>
@end
@implementation TernaryApp {
    TRIHost *_host;
    NSWindow *_window;
    id _keyboard;
    std::map<unsigned short,int> _held;
    int _transpose;
}
- (void)applicationDidFinishLaunching:(NSNotification *)note {
    _host = [TRIHost new];
    [self buildMenu];
    TRIHost *host = _host;
    NSView *editor = TRICreateEditor(
        ^float(int p) { return [host value:p]; },
        ^(int p,float v) { [host setValue:v forParam:p]; },
        ^(unsigned char s,unsigned char a,unsigned char b){ [host sendStatus:s data1:a data2:b]; },
        ^BOOL{ return host.isInputEnabled; },
        ^(BOOL v){ host.inputEnabled=v; },
        ^NSString *{ return host.status; });
    _window = [[NSWindow alloc] initWithContentRect:editor.frame
            styleMask:(NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable)
            backing:NSBackingStoreBuffered defer:NO];
    _window.title = @"Soundnet";
    _window.releasedWhenClosed = NO;
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
- (void)buildMenu {
    NSMenu *bar = [NSMenu new];
    NSMenuItem *appItem = [bar addItemWithTitle:@"Soundnet" action:nil keyEquivalent:@""];
    NSMenu *app = [[NSMenu alloc] initWithTitle:@"Soundnet"];
    appItem.submenu = app;
    [app addItemWithTitle:@"关于 Soundnet" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [app addItem:[NSMenuItem separatorItem]];
    [app addItemWithTitle:@"隐藏 Soundnet" action:@selector(hide:) keyEquivalent:@"h"];
    [app addItemWithTitle:@"退出 Soundnet" action:@selector(terminate:) keyEquivalent:@"q"];
    NSMenuItem *editItem=[bar addItemWithTitle:@"编辑" action:nil keyEquivalent:@""];
    NSMenu *edit=[[NSMenu alloc] initWithTitle:@"编辑"];
    editItem.submenu=edit;
    [edit addItemWithTitle:@"撤销" action:@selector(undo:) keyEquivalent:@"z"];
    [edit addItemWithTitle:@"重做" action:@selector(redo:) keyEquivalent:@"Z"];
    [edit addItem:[NSMenuItem separatorItem]];
    [edit addItemWithTitle:@"剪切" action:@selector(cut:) keyEquivalent:@"x"];
    [edit addItemWithTitle:@"复制" action:@selector(copy:) keyEquivalent:@"c"];
    [edit addItemWithTitle:@"粘贴" action:@selector(paste:) keyEquivalent:@"v"];
    [edit addItemWithTitle:@"全选" action:@selector(selectAll:) keyEquivalent:@"a"];
    NSMenuItem *audioItem = [bar addItemWithTitle:@"音频" action:nil keyEquivalent:@""];
    NSMenu *audio = [[NSMenu alloc] initWithTitle:@"音频"];
    audioItem.submenu = audio;
    NSMenuItem *panic = [audio addItemWithTitle:@"停止所有声音" action:@selector(panic:) keyEquivalent:@"."];
    panic.target = self;
    NSMenuItem *restart = [audio addItemWithTitle:@"重启音频引擎" action:@selector(restart:) keyEquivalent:@""];
    restart.target = self;
    NSMenuItem *helpItem = [bar addItemWithTitle:@"帮助" action:nil keyEquivalent:@""];
    NSMenu *help = [[NSMenu alloc] initWithTitle:@"帮助"];
    helpItem.submenu = help;
    [help addItemWithTitle:@"键盘与音频输入" action:@selector(help:) keyEquivalent:@"?"].target = self;
    NSApp.mainMenu = bar;
}
- (void)panic:(id)sender { _held.clear(); [_host panic]; }
- (void)restart:(id)sender { _held.clear(); [_host restartAudio]; }
- (void)help:(id)sender {
    NSAlert *alert = [NSAlert new];
    alert.messageText = @"演奏 Soundnet";
    alert.informativeText = @"A W S E D F T G Y H U J K 从 C4 演奏至 C5。Z / X 下移或上移八度。⌘. 停止所有声音。\n\nMIDI 键盘自动连接，也可从其他软件发送到「Soundnet MIDI In」。\n\n音频输入来自系统默认输入设备。为避免啸叫，可使用耳机监听，或把输入切到声卡线路输入。";
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
    if (_keyboard) [NSEvent removeMonitor:_keyboard];
    [_host savePatch];
    [_host stop];
}
@end

int main(int argc, const char **argv) {
    @autoreleasepool {
        if (argc==2 && strcmp(argv[1],"--self-test")==0) return [TRIHost runOfflineCheck];
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        __attribute__((objc_precise_lifetime)) TernaryApp *delegate = [TernaryApp new];
        app.delegate = delegate;
        [app run];
        (void)delegate; // Keep alive while NSApplication (weakly) references the delegate.
    }
    return 0;
}
