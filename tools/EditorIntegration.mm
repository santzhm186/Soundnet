// Build on macOS (from the project root):
// xcrun clang++ -O2 -std=c++17 -fobjc-arc tools/EditorIntegration.mm ui/TriEditor.mm ui/WorkflowCanvas.mm -framework Cocoa -framework QuartzCore -o build/editor-check
// ./build/editor-check [optional-preview.png]
#import "../ui/TriEditor.h"
#import "../ui/WorkflowCanvas.h"
#import <objc/runtime.h>
#include "../app/PatchRouting.hpp"
#include "../src/Engine.hpp"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <vector>

static void require(bool passed, NSString *message) {
    if (!passed) {
        fprintf(stderr, "FAIL: %s\n", message.UTF8String);
        std::exit(1);
    }
}
static NSView *findControl(NSView *root, NSString *identifier) {
    if ([root.identifier isEqualToString:identifier]) return root;
    for (NSView *view in root.subviews) {
        if (NSView *match = findControl(view,identifier)) return match;
    }
    return nil;
}
static void inspect(NSView *editor, int node) {
    auto *selector=(NSPopUpButton *)findControl(editor,@"workflow.inspector");
    require([selector isKindOfClass:NSPopUpButton.class],@"missing inspector selector");
    if(selector.selectedItem.tag==node) return;
    require([selector selectItemWithTag:node],@"requested node missing from inspector selector");
    require([selector sendAction:selector.action to:selector.target],@"inspector selection failed");
}
static void inspectParameter(NSView *editor,int p) {
    if (p>=tri::Osc1Attack && p<=tri::Osc3Release) inspect(editor, tri::Osc1Node+(p-tri::Osc1Attack)/3);
    else if (p>=tri::Wave1 && p<=tri::Tune3) inspect(editor,tri::Osc1Node+(p-tri::Wave1)/3);
    else if (p>=tri::Attack && p<=tri::Release) inspect(editor,15);
    else if (p==tri::ArpRepeat || p==tri::Pattern) inspect(editor,tri::ArpNode);
    else if (p>=tri::RouteArpIn && p<=tri::RouteOutputIn) inspect(editor,tri::routeTarget(p));
}
static NSString *identifier(int parameter, NSString *kind) {
    return [NSString stringWithFormat:@"param.%d.%@", parameter,kind];
}
static NSTextField *field(NSView *editor, int param) {
    inspectParameter(editor,param);
    NSString *name = identifier(param,@"field");
    NSView *found = findControl(editor,name);
    require([found isKindOfClass:[NSTextField class]], [@"missing numeric control " stringByAppendingString:name]);
    NSTextField *tf = (NSTextField *)found;
    require(tf.editable && tf.enabled && !tf.hiddenOrHasHiddenAncestor, [@"disabled numeric control " stringByAppendingString:name]);
    return tf;
}
static void edit(NSView *editor, int param, NSString *value) {
    NSTextField *tf = field(editor,param);
    tf.stringValue = value;
    require([tf sendAction:tf.action to:tf.target], [@"input commit failed for " stringByAppendingString:tf.identifier]);
}
static double magnitude(const float *data, int count, double hz) {
    double re=0,im=0;
    for (int i=0; i<count; ++i) {
        double phase = 2.*M_PI*hz*i/48000.;
        re += data[i]*std::cos(phase);
        im += data[i]*std::sin(phase);
    }
    return std::hypot(re,im)/count;
}
// Hold one note with each oscillator an octave apart. Inspect a frequency per
// lane so silence or a shared envelope cannot masquerade as independent ASR.
static void verifyEnvelopeAudio(NSView *editor, tri::Engine &dsp) {
    const int settings[3][3]={{5,90,20},{220,60,150},{500,30,500}};
    for (int lane=0; lane<3; ++lane) {
        dsp.set(tri::Wave1+3*lane,0);
        dsp.set(tri::Level1+3*lane,.52f);
        dsp.set(tri::Tune1+3*lane,12*(lane-1));
        for (int phase=0; phase<3; ++phase) {
            edit(editor,tri::Osc1Attack+lane*3+phase,[NSString stringWithFormat:@"%d",settings[lane][phase]]);
        }
    }
    dsp.set(tri::Master,1); dsp.set(tri::DelayMix,0); dsp.set(tri::Decay,.005);
    dsp.prepare(48000); dsp.midi(0x90,69,127);
    std::vector<float> held(36000), tail(28000), right(held.size());
    dsp.render(nullptr,nullptr,held.data(),right.data(),int(held.size()));
    dsp.midi(0x80,69,0);
    dsp.render(nullptr,nullptr,tail.data(),right.data(),int(tail.size()));
    double early[3], sustain[3], released[3];
    for (int lane=0;lane<3;++lane) {
        double hz=220.*(1<<lane);
        early[lane]=magnitude(held.data()+480,2400,hz);
        sustain[lane]=magnitude(held.data()+30000,2400,hz);
        released[lane]=magnitude(tail.data()+12000,2400,hz);
    }
    require(early[0]>3*early[1] && early[1]>1.2*early[2],@"local attack edits did not change the three partials independently");
    require(sustain[0]>1.35*sustain[1] && sustain[1]>1.7*sustain[2] && sustain[2]>.005,
            @"local sustain edits did not set distinct levels");
    require(released[2]>.001 && released[2]>20*std::max(released[0],released[1]),
            @"local release edits did not preserve just the longest partial");
}
static void verifyRepeatAudio(tri::Engine &dsp) {
    dsp.set(tri::Tempo,120); dsp.set(tri::Pattern,0); dsp.set(tri::ArpOn,1);
    dsp.set(tri::Gate,.8f); dsp.set(tri::DelayMix,0);
    dsp.set(tri::Wave1,0); dsp.set(tri::Level1,1); dsp.set(tri::Tune1,0);
    dsp.set(tri::Level2,0); dsp.set(tri::Level3,0);
    dsp.set(tri::Attack,.001); dsp.set(tri::Sustain,1); dsp.set(tri::Release,.01);
    dsp.prepare(48000);
    dsp.midi(0x90,69,120); dsp.midi(0x90,81,120);
    std::vector<float> left(6*12000), right(left.size());
    dsp.render(nullptr,nullptr,left.data(),right.data(),int(left.size()));
    for(int beat=0; beat<6; ++beat) {
        const float *slice=left.data()+beat*12000+1200;
        double a4=magnitude(slice,4800,440), a5=magnitude(slice,4800,880);
        double desired=beat<3?a4:a5, other=beat<3?a5:a4;
        require(desired>.02 && desired>15*other,
                [NSString stringWithFormat:@"ARP repeat=3 audible pitch mismatch on hit %d",beat+1]);
    }
}
// Keep native editor/menu callbacks intact. Only the system file chooser and
// alert presenter are replaced, so this check can run without human input.
static NSURL *chosenFile;
static NSString *lastAlert;
@interface EditorTestChooser : NSObject
@property(copy) NSString *title, *nameFieldStringValue;
@property(assign) BOOL allowsMultipleSelection;
@end
@implementation EditorTestChooser
-(NSURL *)URL { return chosenFile; }
-(void)beginSheetModalForWindow:(NSWindow *)window completionHandler:(void (^)(NSModalResponse))handler {
    handler(NSModalResponseOK);
}
@end
static id returnChooser(id,SEL) { return [EditorTestChooser new]; }
static void rememberAlert(NSAlert *alert,SEL,NSWindow *,void (^callback)(NSModalResponse)) {
    lastAlert=[alert.informativeText copy];
    if(callback) callback(NSAlertFirstButtonReturn);
}
class ReplacedMethod {
    Method method;
    IMP previous;
public:
    ReplacedMethod(Method m, IMP replacement) : method(m),previous(method_setImplementation(m,replacement)) {}
    ~ReplacedMethod() { method_setImplementation(method,previous); }
};
static NSMenuItem *itemFor(NSView *view,SEL action,int tag) {
    if ([view isKindOfClass:NSPopUpButton.class]) {
        for (NSMenuItem *item in ((NSPopUpButton*)view).itemArray)
            if(item.action==action && item.tag==tag) return item;
    }
    for(NSView *child in view.subviews) if(NSMenuItem *item=itemFor(child,action,tag)) return item;
    return nil;
}
static void flushEvents() {
    [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.015]];
}
static void menuAction(NSView *editor,SEL action,int tag=0) {
    NSMenuItem *item=itemFor(editor,action,tag);
    require(item && item.isEnabled && [NSApp sendAction:item.action to:item.target from:item],
            [@"could not execute " stringByAppendingString:NSStringFromSelector(action)]);
    flushEvents(); // A completed gesture becomes one undo record on the next turn.
}
static void historyAction(NSView *editor,SEL action) {
    for(NSView *v in editor.subviews) {
        if([v isKindOfClass:NSButton.class] && ((NSButton*)v).action==action) {
            NSButton *b=(NSButton*)v;
            require(b.enabled && [b sendAction:b.action to:b.target],@"history action unavailable");
            flushEvents(); return;
        }
    }
    require(false,@"undo/redo button missing");
}
static void selectParameterMenu(NSView *editor,int param,int index) {
    inspectParameter(editor,param);
    NSPopUpButton *menu=(NSPopUpButton*)findControl(editor,identifier(param,@"menu"));
    require([menu isKindOfClass:NSPopUpButton.class],@"parameter menu missing");
    [menu selectItemAtIndex:index];
    require([menu sendAction:menu.action to:menu.target],@"parameter menu action failed");
}
static void connectRoute(NSView *editor,int route,int node) {
    inspectParameter(editor,route);
    NSPopUpButton *menu=(NSPopUpButton*)findControl(editor,[NSString stringWithFormat:@"route.%d",route]);
    require([menu isKindOfClass:NSPopUpButton.class] && [menu.menu itemWithTag:node].enabled,
            @"expected source is absent or disabled in route menu");
    require([menu selectItemWithTag:node] && [menu sendAction:menu.action to:menu.target],
            @"route menu action failed");
}
static void verifyWorkflow(NSView *editor,tri::Engine& dsp,TRIRead read,std::vector<int>& writes) {
    using namespace tri;
    TRIWorkflowCanvas *canvas=(TRIWorkflowCanvas*)findControl(editor,@"workflow.canvas");
    require([canvas isKindOfClass:TRIWorkflowCanvas.class],@"real patch canvas missing");
    flushEvents();
    menuAction(editor,@selector(templates:),3);
    Routing external{};
    external[RouteEQIn-RouteArpIn]=InputNode; external[RouteDelayIn-RouteArpIn]=EQNode;
    external[RouteOutputIn-RouteArpIn]=DelayNode;
    require(dsp.routing()==external,@"external-effects template did not wire Input → EQ → Delay → Output");
    menuAction(editor,@selector(templates:),4);
    require(dsp.routing()==Routing{},@"empty template did not unplug all cables");
    menuAction(editor,@selector(templates:),1);
    require(dsp.routing()==defaultRouting,@"standard template failed to restore factory graph");
    menuAction(editor,@selector(templates:),2);
    auto split=defaultRouting; split[RouteOsc1In-RouteArpIn]=MidiNode;
    require(dsp.routing()==split && dsp.get(tri::Pattern)==1 && dsp.get(ArpRepeat)==2,
            @"layered template did not split MIDI and ARP");
    dsp.set(EQHigh,3.5f); // Host automation after the user's edit.
    historyAction(editor,@selector(undoWorkflow:));
    require(dsp.routing()==defaultRouting && dsp.get(EQHigh)==3.5f,@"undo changed unrelated host automation");
    historyAction(editor,@selector(redoWorkflow:));
    require(dsp.routing()==split && dsp.get(EQHigh)==3.5f,@"redo did not restore the layered route");
    // Remove/reinsert a module using the canvas and native library. Reinserted
    // modules acquire a concrete world position rather than a factory sentinel.
    [canvas removeNode:MixNode]; flushEvents();
    menuAction(editor,@selector(nodeLibrary:),MixNode);
    require(read(layout::x(MixNode))>=0 && read(layout::y(MixNode))>=0,@"re-added Mix lost its canvas position");
    [canvas removeNode:Osc3Node]; flushEvents();
    selectParameterMenu(editor,Wave2,0); edit(editor,Osc2Attack,@"345");
    selectParameterMenu(editor,ArpRepeat,5);
    connectRoute(editor,RouteDelayIn,InputNode);
    connectRoute(editor,RouteEQIn,DelayNode);
    connectRoute(editor,RouteMix1,EQNode);
    connectRoute(editor,RouteMix2,Osc2Node);
    connectRoute(editor,RouteOutputIn,MixNode);
    require(dsp.get(Osc2Attack)==.345f && dsp.get(ArpRepeat)==6 && dsp.get(Wave2)==0,
            @"custom sound was not applied");
    const auto savedRoutes=dsp.routing();
    require(savedRoutes[RouteEQIn-RouteArpIn]==DelayNode && savedRoutes[RouteMix1-RouteArpIn]==EQNode,
            @"custom route did not reach DSP");
    std::array<float,RouteArpIn> savedSound;
    for(int i=0;i<RouteArpIn;++i) savedSound[i]=dsp.get(i);
    layout::State savedLayout;
    for(int i=0;i<layout::count;++i) savedLayout[i]=read(layout::first+i);
    require(read(layout::visibility)==(layout::kDefaultVisible & ~(1<<(Osc3Node-1))),@"remove failed to hide OSC 3");
    flushEvents();
    chosenFile=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:
        [NSUUID.UUID.UUIDString stringByAppendingPathExtension:@"json"]]];
    ReplacedMethod save(class_getClassMethod(NSSavePanel.class,@selector(savePanel)),(IMP)returnChooser);
    ReplacedMethod open(class_getClassMethod(NSOpenPanel.class,@selector(openPanel)),(IMP)returnChooser);
    ReplacedMethod alert(class_getInstanceMethod(NSAlert.class,@selector(beginSheetModalForWindow:completionHandler:)),(IMP)rememberAlert);
    menuAction(editor,@selector(saveWorkflow:));
    NSData *data=[NSData dataWithContentsOfURL:chosenFile];
    require(data.length>0,@"Save Workflow menu did not create a file");
    NSDictionary *file=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    require([file[@"connections"][@"eq.audio"] isEqual:@"delay"] && !file[@"nodes"][@"osc3"] &&
            [file[@"parameters"][@"Arp Repeat"] intValue]==6 && [file[@"nodes"][@"mix"] count]==2,
            @"exported JSON omits routes, sound or module layout");
    menuAction(editor,@selector(templates:),1);
    selectParameterMenu(editor,Wave2,3); edit(editor,Osc2Attack,@"45"); selectParameterMenu(editor,ArpRepeat,0);
    flushEvents();
    writes.clear(); lastAlert=nil;
    menuAction(editor,@selector(openWorkflow:));
    require(lastAlert==nil && dsp.routing()==savedRoutes,@"Open Workflow failed to restore cabling");
    require(std::count(writes.begin(),writes.end(),layout::beginRouting)==1 &&
            std::count(writes.begin(),writes.end(),layout::commitRouting)==1,
            @"opening did not use one complete routing transaction");
    for(int i=0;i<RouteArpIn;++i) require(dsp.get(i)==savedSound[i],@"opening changed a saved synth parameter");
    for(int i=0;i<layout::count;++i) require(read(layout::first+i)==savedLayout[i],@"opening changed saved placement/visibility");
    require(!(canvas.visibleNodeMask & (1<<(Osc3Node-1))),@"canvas did not reflect loaded visibility");
    // Send corrupted real files through Open, including an otherwise valid
    // sound change, so accepting even part of an invalid document is detected.
    for(int variant=0;variant<2;++variant) {
        NSMutableDictionary *broken=[file mutableCopy];
        NSMutableDictionary *parameters=[file[@"parameters"] mutableCopy];
        parameters[@"Arp Repeat"]=@1; broken[@"parameters"]=parameters;
        if(variant==0) {
            NSMutableDictionary *links=[file[@"connections"] mutableCopy];
            links[@"delay.audio"]=@"eq"; broken[@"connections"]=links;
        } else {
            NSMutableDictionary *nodes=[file[@"nodes"] mutableCopy];
            nodes[@"mix"]=@[@(-.5),@100]; broken[@"nodes"]=nodes;
        }
        NSData *bad=[NSJSONSerialization dataWithJSONObject:broken options:0 error:nil];
        require([bad writeToURL:chosenFile atomically:YES],@"could not make invalid workflow fixture");
        writes.clear(); lastAlert=nil;
        menuAction(editor,@selector(openWorkflow:));
        require(lastAlert.length>0 && writes.empty() && dsp.routing()==savedRoutes && dsp.get(ArpRepeat)==6,
                @"invalid workflow was not rejected before all writes");
    }
    require([NSFileManager.defaultManager removeItemAtURL:chosenFile error:nil],@"could not remove test file");
    chosenFile=nil; lastAlert=nil;
}
static void checkEditor(BOOL standalone, NSString *preview) {
    tri::Engine dsp;
    tri::Engine *engine=&dsp;
    std::vector<int> writes;
    auto *written=&writes;
    auto layout=tri::layout::defaults();
    auto *positions=&layout;
    tri::RouteEdit transaction;
    auto *routeEdit=&transaction;
    TRIRead read=^(int id) {
        if(tri::layout::contains(id)) return (*positions)[id-tri::layout::first];
        return engine->get(id);
    };
    TRIWrite write=^(int id,float v) {
        written->push_back(id);
        if(tri::layout::contains(id)) {
            require(tri::layout::valid(id,v),@"editor submitted invalid node layout");
            (*positions)[id-tri::layout::first]=v;
            return;
        }
        if(routeEdit->handle(id,v,[&]{return engine->routing();},[&](const tri::Routing& graph) {
            require(engine->setRouting(graph),@"host rejected graph transaction");
        })) return;
        engine->set(id,v);
    };
    TRIInputRead readInput=standalone?^BOOL{return NO;}: (TRIInputRead)nil;
    TRIInputWrite writeInput=standalone?^(BOOL){}:(TRIInputWrite)nil;
    NSView *editor=TRICreateEditor(read,write,^(uint8_t s,uint8_t a,uint8_t b){engine->midi(s,a,b);},readInput,writeInput,^{ return @"48 kHz · MIDI"; });
    NSWindow *window=[[NSWindow alloc]initWithContentRect:editor.frame styleMask:NSWindowStyleMaskBorderless
                                                  backing:NSBackingStoreBuffered defer:NO];
    window.releasedWhenClosed=NO;
    window.contentView=editor;
    [window display]; [editor layoutSubtreeIfNeeded];
    // Exercise native text editing, including the focus change that commits a Tab.
    NSTextField *attack=field(editor,tri::Osc1Attack);
    [attack selectText:nil];
    NSTextView *typing=(NSTextView *)attack.currentEditor;
    require(typing!=nil,@"could not focus an envelope field");
    [typing insertText:@"437" replacementRange:NSMakeRange(0,typing.string.length)];
    [typing doCommandBySelector:@selector(insertNewline:)];
    require(std::abs(dsp.get(tri::Osc1Attack)-.437f)<1e-6f,@"Return did not commit a typed attack");
    require(window.firstResponder==editor && !attack.currentEditor,
            @"Return kept keyboard input in the field instead of returning to play mode");
    [attack selectText:nil];
    typing=(NSTextView *)attack.currentEditor;
    [typing insertText:@"735" replacementRange:NSMakeRange(0,typing.string.length)];
    writes.clear();
    [typing doCommandBySelector:@selector(cancelOperation:)];
    require(window.firstResponder==editor && !attack.currentEditor && writes.empty() &&
            std::abs(dsp.get(tri::Osc1Attack)-.437f)<1e-6f && [attack.stringValue isEqualToString:@"437"],
            @"Escape did not discard the pending edit and return to play mode");
    [attack selectText:nil];
    typing=(NSTextView *)attack.currentEditor;
    [typing insertText:@"" replacementRange:NSMakeRange(0,typing.string.length)];
    [typing doCommandBySelector:@selector(insertTab:)];
    require(dsp.get(tri::Osc1Attack)==tri::Follow,@"Tab failed to commit a cleared cell");
    [window makeFirstResponder:nil];
    // Each lane follows a different A/S/R combination to expose misrouted IDs or displayed units.
    const int first=tri::Osc1Attack;
    float expect[9];
    for(int lane=0;lane<3;++lane) for(int phase=0;phase<3;++phase) {
        int i=lane*3+phase;
        int typed=phase==1 ? 21+lane*19 : 170+61*i;
        float value=typed/(phase==1?100.f:1000.f);
        expect[i]=value;
        require(dsp.get(first+i)==tri::Follow,@"new local envelope does not follow by default");
        writes.clear(); edit(editor,first+i,[NSString stringWithFormat:@"%d",typed]);
        require(writes.size()==1 && writes[0]==first+i,@"editing a lane also changed an unrelated parameter");
        require(std::abs(dsp.get(first+i)-value)<1e-5,@"time/percent input conversion mismatch");
    }
    NSString *stepID=identifier(tri::Osc3Release,@"stepper");
    NSView *stepView=findControl(editor,stepID);
    require([stepView isKindOfClass:NSStepper.class],@"release stepper missing");
    NSStepper *step=(NSStepper *)stepView;
    step.doubleValue+=step.increment;
    require([step sendAction:step.action to:step.target],@"release stepper action failed");
    expect[8]+=.001f;
    require(std::abs(dsp.get(tri::Osc3Release)-expect[8])<1e-6,@"release stepper lost its millisecond units");
    // Clearing one lane returns exactly that cell to the shared envelope.
    edit(editor,first+4,@""); expect[4]=tri::Follow;
    for(int i=0;i<9;++i) require(std::abs(dsp.get(first+i)-expect[i])<1e-5,@"edits leaked across envelope cells");
    // An explicit 0 remains distinct from following the shared envelope.
    edit(editor,first+4,@"0"); require(dsp.get(first+4)==0,@"explicit zero stored as Follow");
    NSString *repeatID=identifier(tri::ArpRepeat,@"menu");
    inspect(editor,tri::ArpNode);
    NSView *found=findControl(editor,repeatID);
    require([found isKindOfClass:NSPopUpButton.class],@"ARP repeat selector missing");
    NSPopUpButton *menu=(NSPopUpButton *)found;
    require(menu.numberOfItems==8,@"unexpected number of repeats");
    for(int repetitions:{8,1,3}) {
        [menu selectItemAtIndex:repetitions-1];
        writes.clear();
        require([menu sendAction:menu.action to:menu.target],@"ARP repeat commit failed");
        require(writes.size()==1 && writes[0]==tri::ArpRepeat && dsp.get(tri::ArpRepeat)==repetitions,
                @"ARP repeat selector wrote the wrong DSP value");
    }
    writes.clear();
    require([menu sendAction:menu.action to:menu.target] && writes.empty(),@"unchanged repeat wrote an automation point");
    if (preview) {
        [window display]; [editor layoutSubtreeIfNeeded];
        NSBitmapImageRep *image=[editor bitmapImageRepForCachingDisplayInRect:editor.bounds];
        [editor cacheDisplayInRect:editor.bounds toBitmapImageRep:image];
        NSData *png=[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        require([png writeToFile:preview atomically:YES],@"could not save test preview");
    }
    verifyEnvelopeAudio(editor,dsp);
    for(int i=0;i<9;++i) edit(editor,first+i,@"");
    verifyRepeatAudio(dsp);
    verifyWorkflow(editor,dsp,read,writes);
    [window close];
    printf("PASS — %s editor: Return/Tab commits, Return/Escape restoring play focus, Escape leaving the patch untouched, a 1 ms step, all nine A/S/R fields, ms/percent scaling, explicit zero vs. follow, isolated writes, spectral attack/sustain/release separation, repeat menu and its rendered A4/A4/A4/A5/A5/A5 sequence; four workflow menu templates, undo/redo preserving external automation, real JSON file roundtrip for DSP/routing/position/visibility, rejection of cycles and invalid coordinates before writes.\n",standalone?"standalone":"AU");
}
int main(int argc,const char **argv) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        checkEditor(YES, argc>1 ? @(argv[1]) : nil);
        checkEditor(NO,nil);
    }
    return 0;
}
