#import "../ui/WorkflowCanvas.h"
#include "../src/Engine.hpp"
#include "../app/PatchLayout.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

static void need(bool ok,const char *message) {
    if (!ok) { fprintf(stderr,"FAIL: %s\n",message);std::exit(1); }
}
// Drive the actual canvas with AppKit mouse events at coordinates in its
// factory layout. This also detects non-interactive or unreachable jacks.
static NSPoint viewPoint(double x,double y) {
    double s=(853.-40.)/830.;
    return NSMakePoint(853./2+(x-427.)*s,463./2-7+(y-190.5)*s);
}
static void drag(TRIWorkflowCanvas *canvas,NSPoint a,NSPoint b,NSEventModifierFlags flags=0) {
    auto event=[&](NSEventType type,NSPoint p){
        return [NSEvent mouseEventWithType:type location:[canvas convertPoint:p toView:nil]
            modifierFlags:flags timestamp:0 windowNumber:canvas.window.windowNumber
            context:nil eventNumber:1 clickCount:1 pressure:1];
    };
    [canvas mouseDown:event(NSEventTypeLeftMouseDown,a)];
    [canvas mouseDragged:event(NSEventTypeLeftMouseDragged,b)];
    [canvas mouseUp:event(NSEventTypeLeftMouseUp,b)];
}
int main(int argc,const char **argv) {@autoreleasepool{
    [NSApplication sharedApplication];
    tri::Engine engine;
    engine.prepare(48000);
    auto layout=tri::layout::defaults();
    auto *state=&engine; auto *positions=&layout;
    __block BOOL batch=NO;
    __block tri::Routing pending=engine.routing();
    __block int selected=tri::Osc1Node;
    TRIWorkflowCanvas *canvas=[[TRIWorkflowCanvas alloc]initWithFrame:NSMakeRect(0,0,853,463)
        read:^(int p){return tri::layout::contains(p)?(*positions)[p-tri::layout::first]:state->get(p);}
        write:^(int p,float v){
            if(tri::layout::contains(p)){
                need(tri::layout::valid(p,v),"invalid layout value");(*positions)[p-tri::layout::first]=v;
            }else if(p==tri::layout::beginRouting){batch=YES;pending=state->routing();}
            else if(p==tri::layout::commitRouting){need(batch && state->setRouting(pending),"invalid route transaction");batch=NO;}
            else if(batch){pending[p-tri::RouteArpIn]=(int)v;}else{state->set(p,v);}
        } selected:^(int n){selected=n;}];
    NSWindow *window=[[NSWindow alloc]initWithContentRect:canvas.frame styleMask:NSWindowStyleMaskBorderless
                                                  backing:NSBackingStoreBuffered defer:NO];
    window.contentView=canvas;window.releasedWhenClosed=NO;[window display];
    if(argc>1){
        NSBitmapImageRep *image=[canvas bitmapImageRepForCachingDisplayInRect:canvas.bounds];
        [canvas cacheDisplayInRect:canvas.bounds toBitmapImageRep:image];
        need([[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}]writeToFile:@(argv[1]) atomically:YES],"screenshot failed");
    }
    // Replace the default effects return with a direct external-audio-to-output cable.
    NSPoint ext=viewPoint(341,349),out=viewPoint(759,191),midi=viewPoint(90,191);
    drag(canvas,ext,out);need(engine.get(tri::RouteOutputIn)==tri::InputNode,"actual jack drag did not replace output");
    std::vector<float> input(768),l(768),r(768);
    input[40]=.43f;engine.midi(0x90,69,127);
    engine.render(input.data(),nullptr,l.data(),r.data(),(int)l.size());
    need(l[40]>.2f && std::abs(l[500])<1e-6,"drawn cable did not change the rendered sound");
    // Audio targets reject MIDI sources. A return from Delay into EQ would produce a cycle.
    drag(canvas,midi,out);need(engine.get(tri::RouteOutputIn)==tri::InputNode,"accepted a type mismatch");
    drag(canvas,viewPoint(725,191),viewPoint(512,191));need(engine.get(tri::RouteEQIn)==tri::MixNode,"accepted a feedback cycle");
    // Move the cable from Output to Mix input 2; fan-out at Mix input 4 still exists.
    drag(canvas,out,viewPoint(381,182));
    need(engine.get(tri::RouteOutputIn)==tri::NoNode && engine.get(tri::RouteMix2)==tri::InputNode &&
         engine.get(tri::RouteMix4)==tri::InputNode,"input-to-input move or fan-out failed");
    drag(canvas,viewPoint(381,182),viewPoint(422,285));
    need(engine.get(tri::RouteMix2)==tri::NoNode,"dropping an input in blank space failed to disconnect");
    // Click on a node body, move it, and check that layout state records its world position.
    drag(canvas,viewPoint(248,123),viewPoint(274,137));
    need(selected==tri::Osc2Node && std::abs(layout[tri::layout::x(tri::Osc2Node)-tri::layout::first]-256)<.01,
         "selection or persisted drag failed");
    CGFloat previousZoom=canvas.graphZoom;[canvas zoomIn];need(canvas.graphZoom>previousZoom,"zoom did not change");
    [canvas resetLayout];need(layout[tri::layout::x(tri::Osc2Node)-tri::layout::first]==-1,"layout reset did not restore fallback");
    [canvas removeNode:tri::InputNode];
    need(!(canvas.visibleNodeMask & (1<<(tri::InputNode-1))) && engine.get(tri::RouteMix4)==tri::NoNode,"removal left visible node or cable");
    [canvas addNode:tri::InputNode];
    need((canvas.visibleNodeMask & (1<<(tri::InputNode-1))) && selected==tri::InputNode,"restore/select failed");
    puts("PASS: AppKit canvas drag changed DSP output; MIDI/audio typing and cycle protection; input reassignment, disconnect, fan-out, selection, drag persistence, zoom, reset, remove and restore.");
    return 0;
}}
