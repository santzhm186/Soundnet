#import "../ui/TriEditor.h"
#import "../ui/SNLocalization.h"
#include "../ui/CanvasZones.hpp"
#import "../src/WorkflowAssets.h"
#include "../src/Engine.hpp"
#include "../platform/ProjectDocument.hpp"
#include "../src/WavFile.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <functional>
#include <memory>
#include <cassert>
#include <unistd.h>

@interface NSView (SoundnetTestActions)
- (void)insert:(NSMenuItem *)item;
- (void)undo:(id)sender;
- (void)redo:(id)sender;
- (void)commit:(NSDictionary *)previous;
- (void)fit:(id)sender;
- (void)inspect;
- (void)selectAll:(id)sender;
- (void)copy:(id)sender;
- (void)paste:(id)sender;
- (void)remove:(id)sender;
- (NSArray *)activeIDs;
- (BOOL)dropFileURLs:(NSArray<NSURL *> *)urls atPoint:(NSPoint)point clipMode:(BOOL)clipMode;
- (BOOL)adoptFile:(NSDictionary *)file;
- (NSDictionary *)migrate:(NSDictionary *)file;
- (void)sliderChanged:(NSSlider *)slider;
- (void)choiceChanged:(NSPopUpButton *)popup;
- (void)nameChanged:(NSTextField *)field;
- (void)bypassChanged:(NSButton *)button;
- (void)selectOnly:(uint64_t)uid;
- (void)stopGraph:(id)sender;
- (void)playGraph:(id)sender;
- (void)trigger:(id)sender;
- (void)languageChanged:(NSPopUpButton *)sender;
- (void)toggleParameterGroup:(NSButton *)sender;
- (void)toggleLibrary:(id)sender;
- (void)cancelProcessing:(id)sender;
- (void)importScore:(id)sender;
- (void)selectOnly:(uint64_t)uid;
- (uint64_t)insertNodeType:(int)type atPoint:(NSPoint)point;
- (void)collapseAll:(id)sender;
- (void)expandAll:(id)sender;
- (void)groupSelection:(id)sender;
- (NSRect)cardRectForNode:(NSDictionary *)node;
@end

@interface SoundnetLibraryDrag : NSObject
@property(nonatomic,strong) NSPasteboardItem *draggingPasteboard;
@property(nonatomic) NSPoint draggingLocation;
@end
@implementation SoundnetLibraryDrag
@end

static NSButton *buttonWithAction(NSView *view,SEL action) {
    if([view isKindOfClass:NSButton.class]&&((NSButton *)view).action==action)return (NSButton *)view;
    for(NSView *child in view.subviews)if(NSButton *found=buttonWithAction(child,action))return found;
    return nil;
}
static NSDictionary *snapshot(NSView *editor) {
    NSData *data=[NSJSONSerialization dataWithJSONObject:[editor valueForKey:@"document"] options:0 error:nil];
    return [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
}

static void savePreview(NSView *editor,NSString *path) {
    [editor.window display];
    NSBitmapImageRep *image=[editor bitmapImageRepForCachingDisplayInRect:editor.bounds];
    [editor cacheDisplayInRect:editor.bounds toBitmapImageRep:image];
    assert([[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES]);
}

static void waitForJob(NSView *editor) {
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:10];
    while([[editor valueForKey:@"exportBusy"] boolValue] && deadline.timeIntervalSinceNow>0)
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    assert(![[editor valueForKey:@"exportBusy"] boolValue]);
}

static NSEvent *canvasMouse(NSView *canvas,NSEventType type,NSPoint point) {
    return [NSEvent mouseEventWithType:type location:[canvas convertPoint:point toView:nil]
        modifierFlags:0 timestamp:0 windowNumber:canvas.window.windowNumber context:nil
        eventNumber:0 clickCount:1 pressure:1];
}

static void verifyCableGesture(NSView *editor,NSDictionary *audioAsset) {
    [editor setValue:tri::newProjectDocument() forKey:@"document"];
    [editor setValue:@1 forKey:@"zoom"];
    [editor setValue:[NSValue valueWithPoint:NSZeroPoint] forKey:@"origin"];
    NSView *canvas=[editor valueForKey:@"canvas"];
    const auto add=[&](const char *key,NSPoint at) {
        return [editor insertNodeType:tri::nodeTypeForKey(key) atPoint:at];
    };
    const auto clip=add("soundnet.audio-clip",NSMakePoint(20,330));
    assert(clip && add("soundnet.compressor",NSMakePoint(520,125)));
    assert(add("soundnet.oscillator",NSMakePoint(265,290)));
    NSMutableDictionary *clipNode=[[editor valueForKey:@"document"][@"nodes"] filteredArrayUsingPredicate:
        [NSPredicate predicateWithFormat:@"id == %@",@(clip)]].firstObject;
    clipNode[@"asset"]=audioAsset;
    [editor commit:nil];
    [editor selectOnly:clip];
    const NSPoint from=NSMakePoint(192,377),cursor=NSMakePoint(485,207),valid=NSMakePoint(520,172),invalid=NSMakePoint(265,337);
    NSDictionary *before=snapshot(editor);
    [canvas mouseDown:canvasMouse(canvas,NSEventTypeLeftMouseDown,from)];
    [canvas mouseDragged:canvasMouse(canvas,NSEventTypeLeftMouseDragged,cursor)];
    assert([snapshot(editor) isEqual:before]);
    assert([[editor valueForKey:@"wireSource"] unsignedLongLongValue]==clip);
    assert(NSEqualPoints([[editor valueForKey:@"wireCursor"] pointValue],cursor));
    // Exercise drawing and background thumbnail publication during the drag.
    NSBitmapImageRep *canvasImage=[canvas bitmapImageRepForCachingDisplayInRect:canvas.bounds];
    [canvas cacheDisplayInRect:canvas.bounds toBitmapImageRep:canvasImage];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:3];
    NSMapTable *waveforms=[canvas valueForKey:@"waveforms"];
    while(![waveforms objectForKey:audioAsset] && deadline.timeIntervalSinceNow>0)
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    assert([[waveforms objectForKey:audioAsset] length]>0);
    NSView *library=[editor valueForKey:@"library"];
    [library collapseAll:nil];
    savePreview(editor,@"build/qa/creative-canvas.png");
    [library expandAll:nil];
    [canvas mouseUp:canvasMouse(canvas,NSEventTypeLeftMouseUp,invalid)];
    assert([snapshot(editor) isEqual:before] && ![[editor valueForKey:@"wireSource"] unsignedLongLongValue]);
    [canvas mouseDown:canvasMouse(canvas,NSEventTypeLeftMouseDown,from)];
    [canvas mouseDragged:canvasMouse(canvas,NSEventTypeLeftMouseDragged,valid)];
    [canvas mouseUp:canvasMouse(canvas,NSEventTypeLeftMouseUp,valid)];
    NSArray *edges=snapshot(editor)[@"edges"];
    assert(edges.count==1 && [edges.firstObject[@"source"] unsignedLongLongValue]==clip);
    assert([edges.firstObject[@"port"] isEqual:@"audio"]);

    // Both endpoints are offscreen, but the straight section of this cable
    // still crosses the viewport and must be painted.
    NSMutableDictionary *document=[editor valueForKey:@"document"];
    NSMutableDictionary *crossing=[document mutableCopy];
    NSMutableArray *distant=[NSMutableArray new];
    const double width=NSWidth(canvas.bounds);
    for(NSDictionary *row in document[@"nodes"]) {
        NSMutableDictionary *copy=[row mutableCopy];
        const bool source=[row[@"id"] isEqual:edges.firstObject[@"source"]];
        const bool target=[row[@"id"] isEqual:edges.firstObject[@"target"]];
        copy[@"x"]=@(source?-280.:width+120.);
        copy[@"y"]=@(source || target?100.:1000.);
        [distant addObject:copy];
    }
    crossing[@"nodes"]=distant;
    NSMutableDictionary *disconnected=[crossing mutableCopy];
    disconnected[@"edges"]=@[];
    const auto greenAtCable=[&](NSDictionary *graph) {
        [editor setValue:graph forKey:@"document"];
        NSBitmapImageRep *image=[canvas bitmapImageRepForCachingDisplayInRect:canvas.bounds];
        [canvas cacheDisplayInRect:canvas.bounds toBitmapImageRep:image];
        const double scale=image.pixelsWide/width;
        return [[image colorAtX:image.pixelsWide/2 y:std::lround(147*scale)]
            colorUsingColorSpace:NSColorSpace.sRGBColorSpace].greenComponent;
    };
    assert(greenAtCable(crossing)>greenAtCable(disconnected)+.2);
    [editor setValue:document forKey:@"document"];
    [editor undo:nil];
    assert([snapshot(editor) isEqual:before]);
}

static void verifyAdaptiveZones(NSView *editor) {
    using namespace soundnet::canvas;
    std::array<StageExtent,stageCount> extents{};
    assert(makeZones(extents).placementX(Stage::Output,20)==20);
    extents[0].add(100);extents[1].add(330);extents[2].add(550);extents[3].add(1050);
    const auto beforeExpansion=makeZones(extents);
    assert(beforeExpansion.placementX(Stage::Effects,600)==600);
    assert(beforeExpansion.placementX(Stage::Effects,310)>beforeExpansion.bands[1].right);
    extents[2].add(790);
    const auto expanded=makeZones(extents);
    assert(expanded.bands[2].right>beforeExpansion.bands[2].right);
    assert(expanded.bands[2].center()>beforeExpansion.bands[2].center());
    extents[0].add(2400); // A legacy graph can put stages in reverse order.
    const auto legacy=makeZones(extents);
    for(const auto& band:legacy.bands)assert(band.left<band.right);

    [editor setValue:tri::newProjectDocument() forKey:@"document"];
    [editor setValue:@1 forKey:@"zoom"];
    [editor setValue:@NO forKey:@"snap"];
    [editor setValue:[NSValue valueWithPoint:NSZeroPoint] forKey:@"origin"];
    NSView *canvas=[editor valueForKey:@"canvas"];
    const auto add=[&](const char *key,NSPoint at) {
        const auto uid=[editor insertNodeType:tri::nodeTypeForKey(key) atPoint:at];
        assert(uid);
        return @(uid);
    };
    const auto card=[&](NSNumber *uid) {
        return [snapshot(editor)[@"nodes"] filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"id == %@",uid]].firstObject;
    };
    NSNumber *clip=add("soundnet.audio-clip",NSMakePoint(42,134));
    add("soundnet.sampler",NSMakePoint(270,179));
    NSNumber *effect=add("soundnet.compressor",NSMakePoint(500,106));
    assert([card(clip)[@"x"] doubleValue]==42 && [card(effect)[@"x"] doubleValue]==500);

    const auto drag=[&](NSPoint from,NSPoint to,bool correction) {
        NSDictionary *before=snapshot(editor);
        [canvas mouseDown:canvasMouse(canvas,NSEventTypeLeftMouseDown,from)];
        [canvas mouseDragged:canvasMouse(canvas,NSEventTypeLeftMouseDragged,to)];
        assert([snapshot(editor) isEqual:before]);
        assert(NSIsEmptyRect([[canvas valueForKey:@"placementPreview"] rectValue])!=correction);
        [canvas mouseUp:canvasMouse(canvas,NSEventTypeLeftMouseUp,to)];
        assert(NSIsEmptyRect([[canvas valueForKey:@"placementPreview"] rectValue]));
    };
    drag(NSMakePoint(520,126),NSMakePoint(544,143),false);
    assert([card(effect)[@"x"] doubleValue]==524 && [card(effect)[@"y"] doubleValue]==123);
    drag(NSMakePoint(544,143),NSMakePoint(310,400),true);
    assert([card(effect)[@"x"] doubleValue]>400 && [card(effect)[@"y"] doubleValue]==380);
    [editor undo:nil];
    assert([card(effect)[@"x"] doubleValue]==524 && [card(effect)[@"y"] doubleValue]==123);
    NSNumber *output=add("soundnet.audio-output",NSMakePoint(290,450));
    assert([card(output)[@"x"] doubleValue]>[card(effect)[@"x"] doubleValue]);
    [editor undo:nil];
    assert(!card(output));
}

static void verifyOuterTintAndInsertion(NSView *editor) {
    [editor setValue:tri::newProjectDocument() forKey:@"document"];
    [editor setValue:@1 forKey:@"zoom"];
    NSView *canvas=[editor valueForKey:@"canvas"];
    const auto add=[&](const char *key,NSPoint at) {
        const auto uid=[editor insertNodeType:tri::nodeTypeForKey(key) atPoint:at];
        assert(uid);
        return @(uid);
    };
    const auto tint=[&](double originX) {
        [editor setValue:[NSValue valueWithPoint:NSMakePoint(originX,0)] forKey:@"origin"];
        canvas.needsDisplay=YES;
        NSBitmapImageRep *image=[canvas bitmapImageRepForCachingDisplayInRect:canvas.bounds];
        [canvas cacheDisplayInRect:canvas.bounds toBitmapImageRep:image];
        return [[image colorAtX:24 y:110] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    };
    add("soundnet.gain",NSMakePoint(450,115));
    NSColor *left=tint(10000),*right=tint(-10000);
    assert(left.greenComponent>left.redComponent && [left isEqual:right]);
    add("soundnet.chord",NSMakePoint(235,370));
    left=tint(10000);right=tint(-10000);
    assert(left.redComponent>left.greenComponent && right.greenComponent>right.redComponent);

    using namespace soundnet::canvas;
    Extents extents{};
    extents[int(Stage::Sources)].add(100);
    extents[int(Stage::Output)].add(630);
    const auto slack=planZoneInsertion(extents,Stage::Effects,300,212);
    assert(slack.fits && slack.center==312 && slack.shifts==ZoneShifts{});
    const auto beforeZones=makeZones(extents);
    extents[int(Stage::Effects)].add(slack.center);
    const auto afterZones=makeZones(extents);
    assert(beforeZones.bands[int(Stage::Sources)].right>afterZones.bands[int(Stage::Sources)].right);
    extents[int(Stage::Output)]={};
    extents[int(Stage::Output)].add(320);
    const auto blocked=planZoneInsertion(extents,Stage::Effects,200,212);
    assert(!blocked.fits && blocked.shifts==ZoneShifts{});
    const auto expanded=planZoneInsertion(extents,Stage::Effects,200,212,1000);
    assert(expanded.fits && expanded.shifts[int(Stage::Output)]-expanded.shifts[int(Stage::Sources)]==204);

    [editor setValue:tri::newProjectDocument() forKey:@"document"];
    [editor setValue:[NSValue valueWithPoint:NSZeroPoint] forKey:@"origin"];
    NSNumber *first=add("soundnet.oscillator",NSMakePoint(235,115));
    NSNumber *second=add("soundnet.oscillator",NSMakePoint(235,295));
    NSNumber *output=add("soundnet.audio-output",NSMakePoint(630,405));
    add("soundnet.audio-output",NSMakePoint(630,200));
    add("soundnet.audio-output",NSMakePoint(630,20));
    NSMutableSet *selection=[editor valueForKey:@"selection"];
    [selection setSet:[NSSet setWithArray:@[first,second]]];
    [editor groupSelection:nil];
    const auto card=[&](NSNumber *uid) {
        return [snapshot(editor)[@"nodes"] filteredArrayUsingPredicate:
            [NSPredicate predicateWithFormat:@"id == %@",uid]].firstObject;
    };
    NSDictionary *before=snapshot(editor);
    NSNumber *inserted=add("soundnet.gain",NSMakePoint(418,170));
    // A 29-point deficit. The left region gives way as a unit.
    assert([card(inserted)[@"x"] doubleValue]==418 && [card(inserted)[@"y"] doubleValue]==170);
    assert([card(first)[@"x"] doubleValue]==206 && [card(first)[@"y"] doubleValue]==115);
    assert([card(second)[@"x"] doubleValue]==206 && [card(second)[@"y"] doubleValue]==295);
    assert([card(output)[@"x"] doubleValue]==630 && [card(output)[@"y"] doubleValue]==405);
    [editor undo:nil];
    assert([snapshot(editor) isEqual:before]);

    // A library drag displaces the whole region on screen before touching
    // the document. Cancellation restores the presentation without history.
    SoundnetLibraryDrag *drag=[SoundnetLibraryDrag new];
    drag.draggingPasteboard=[NSPasteboardItem new];
    [drag.draggingPasteboard setString:[@(tri::nodeTypeForKey("soundnet.gain")) stringValue]
                               forType:@"com.soundnet.node-type"];
    drag.draggingLocation=[canvas convertPoint:NSMakePoint(418+86,170+40) toView:nil];
    id<NSDraggingInfo> sender=(id<NSDraggingInfo>)drag;
    const NSUInteger undoCount=[[editor valueForKey:@"history"] count];
    assert([canvas draggingEntered:sender]==NSDragOperationCopy);
    assert([canvas draggingUpdated:sender]==NSDragOperationCopy);
    const NSRect previewFirst=[canvas cardRectForNode:card(first)];
    const NSRect previewSecond=[canvas cardRectForNode:card(second)];
    assert(previewFirst.origin.x==206 && previewFirst.origin.y==115);
    assert(previewSecond.origin.x==206 && previewSecond.origin.y==295);
    assert([canvas cardRectForNode:card(output)].origin.x==630);
    assert([snapshot(editor) isEqual:before] && [[editor valueForKey:@"history"] count]==undoCount);
    [canvas draggingExited:sender];
    assert([canvas cardRectForNode:card(first)].origin.x==235);
    assert([snapshot(editor) isEqual:before] && [[editor valueForKey:@"history"] count]==undoCount);
    assert([canvas draggingEntered:sender]==NSDragOperationCopy);
    [canvas draggingEnded:sender];
    assert([canvas cardRectForNode:card(first)].origin.x==235);
    assert([snapshot(editor) isEqual:before] && [[editor valueForKey:@"history"] count]==undoCount);

    assert([canvas draggingEntered:sender]==NSDragOperationCopy);
    assert([canvas performDragOperation:sender]);
    assert(NSEqualRects([canvas cardRectForNode:card(first)],previewFirst));
    assert(NSEqualRects([canvas cardRectForNode:card(second)],previewSecond));
    assert([card(first)[@"x"] doubleValue]==206 && [card(second)[@"x"] doubleValue]==206);
    assert([snapshot(editor)[@"groups"][0][@"x"] doubleValue]==[before[@"groups"][0][@"x"] doubleValue]-29);
    assert([[editor valueForKey:@"history"] count]==undoCount+1);
    [editor undo:nil];
    assert([snapshot(editor) isEqual:before]);

    // A same-region overlap moves only the card being inserted.
    inserted=add("soundnet.oscillator",NSMakePoint(235,115));
    assert([card(inserted)[@"y"] doubleValue]!=115);
    assert([card(first)[@"y"] doubleValue]==115 && [card(second)[@"y"] doubleValue]==295);
    [editor undo:nil];
    assert([snapshot(editor) isEqual:before]);
}

int main() {
    @autoreleasepool {
        BOOL ownPreferences=!getenv("SOUNDNET_PREFERENCES_SUITE");
        NSString *preferenceSuite=ownPreferences?[NSString stringWithFormat:@"com.soundnet.editor-tests.%d",getpid()]:@(getenv("SOUNDNET_PREFERENCES_SUITE"));
        if(ownPreferences)setenv("SOUNDNET_PREFERENCES_SUITE",preferenceSuite.UTF8String,1);
        [NSApplication sharedApplication];
        id originalLanguage=[SNLanguagePreferences() objectForKey:@"SoundnetLanguage"];
        BOOL originalEnglish=SNIsEnglish();
        SNSetEnglish(NO);
        auto engine = std::make_shared<tri::Engine>();
        __block NSDictionary *assets = @{};
        NSView *editor = TRICreateEditor(
            ^float(int p) { return engine->get(p); },
            ^(int p, float value) { engine->set(p, value); },
            ^(uint8_t s, uint8_t a, uint8_t b) { engine->midi(s,a,b); },
            ^BOOL { return NO; }, ^(BOOL) {}, ^NSString *{ return @"TEST"; },
            ^NSDictionary *{ return assets; },
            ^BOOL(NSDictionary *replacement) {
                tri::GraphSpec graph;
                if (!triassets::decodeGraph(replacement[@"graph"],graph,nullptr)) return NO;
                if (!engine->setGraph(graph)) return NO;
                assets = replacement;
                return YES;
            },
            ^BOOL { return engine->processingEnabled(); },
            ^(BOOL enabled) { engine->setProcessingEnabled(enabled); });
        NSDictionary *initialDocument=snapshot(editor);
        const NSUInteger initialNodeCount=[initialDocument[@"nodes"] count];
        assert(initialNodeCount==2);
        assert([initialDocument[@"settings"][@"environmentRequired"] boolValue]);
        NSSet *environmentKeys=[NSSet setWithArray:[initialDocument[@"nodes"] valueForKey:@"nodeKey"]];
        assert(([environmentKeys isEqualToSet:[NSSet setWithArray:@[@"soundnet.timecode",@"soundnet.project-settings"]]]));
        for (int i=0; i<25; ++i) {
            NSMenuItem *item = [NSMenuItem new];
            item.tag = tri::nodeTypeID("soundnet.oscillator");
            [editor insert:item];
        }
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+25);
        NSMenuItem *start = [NSMenuItem new];
        start.tag = tri::nodeTypeID("soundnet.start");
        [editor insert:start];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+26);
        [editor undo:nil];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+25);
        [editor redo:nil];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+26);
        [editor selectAll:nil];
        assert([[editor activeIDs] count]==initialNodeCount+26);
        // Deleting all ordinary nodes retains the exact project environment;
        // undo still restores every user node as one document transaction.
        NSDictionary *beforeDeleteAll=snapshot(editor);
        [editor remove:nil];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount);
        assert([assets[@"graph"][@"nodes"] isEqual:initialDocument[@"nodes"]]);
        [editor undo:nil];assert([snapshot(editor) isEqual:beforeDeleteAll]);
        [editor selectAll:nil];
        [editor copy:nil];
        [editor paste:nil];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+52);
        assert([[editor activeIDs] count]==26);
        [editor remove:nil];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+26);
        [editor undo:nil];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+52);
        [editor redo:nil];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+26);
        NSMenuItem *sampler = [NSMenuItem new];
        sampler.tag = tri::nodeTypeID("soundnet.sampler");
        [editor insert:sampler];
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+27);
        NSString *samplerKey=[NSString stringWithUTF8String:tri::nodeKey(tri::nodeTypeID("soundnet.sampler"))];
        NSDictionary *samplerNode = [[assets[@"graph"][@"nodes"] filteredArrayUsingPredicate:
            [NSPredicate predicateWithFormat:@"nodeKey == %@", samplerKey]] firstObject];
        assert(samplerNode && [samplerNode[@"parameters"] count]==10);
        std::vector<float> wave(4800), waveR(4800);
        for (size_t i=0; i<wave.size(); ++i) wave[i]=waveR[i]=0.2f*std::sin(2*M_PI*440*i/48000.0);
        std::string why;
        const std::string dropPath = "/tmp/soundnet-drop-test.wav";
        assert(tri::wav::write(dropPath, wave, waveR, 48000, why));
        NSURL *dropURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:dropPath.c_str()]];
        assert([editor dropFileURLs:@[dropURL] atPoint:NSMakePoint(260, 100) clipMode:NO]);
        waitForJob(editor);
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+28);
        NSDictionary *samplerDrop = [assets[@"graph"][@"nodes"] lastObject];
        assert(samplerDrop && [samplerDrop[@"nodeKey"] isEqual:samplerKey] && samplerDrop[@"asset"][@"name"]);
        assert([samplerDrop[@"x"] doubleValue]==260 && [samplerDrop[@"y"] doubleValue]==100);
        assert([editor dropFileURLs:@[dropURL] atPoint:NSMakePoint(140, 140) clipMode:YES]);
        waitForJob(editor);
        assert([assets[@"graph"][@"nodes"] count]==initialNodeCount+29);
        NSDictionary *clipDrop = [assets[@"graph"][@"nodes"] lastObject];
        NSString *clipKey=[NSString stringWithUTF8String:tri::nodeKey(tri::nodeTypeID("soundnet.audio-clip"))];
        assert(clipDrop && [clipDrop[@"nodeKey"] isEqual:clipKey] && clipDrop[@"asset"][@"name"]);
        assert([clipDrop[@"x"] doubleValue]<[samplerDrop[@"x"] doubleValue]);

        // A drop is accepted before decoding. Cancellation must discard every
        // file in the batch, and an outstanding job must reject another drop.
        NSDictionary *beforeCancelledDrop=snapshot(editor);
        assert(([editor dropFileURLs:@[dropURL,dropURL] atPoint:NSMakePoint(180,180) clipMode:NO]));
        assert([[editor valueForKey:@"exportBusy"] boolValue]);
        assert(![editor dropFileURLs:@[dropURL] atPoint:NSMakePoint(200,200) clipMode:NO]);
        [editor cancelProcessing:nil];
        waitForJob(editor);
        assert([snapshot(editor) isEqual:beforeCancelledDrop]);

        // Historical patch formats are rejected explicitly by this platform.
        NSDictionary *beforeLegacy=snapshot(editor);
        assert((![editor adoptFile:@{@"format":@"audio.ternary.workflow",@"version":@2}]));
        assert([snapshot(editor) isEqual:beforeLegacy]);

        // Opening a v4 project must go through the canonicaliser, not the v1/v2
        // migrator.
        NSDictionary *v4=[NSJSONSerialization JSONObjectWithData:
            [NSData dataWithContentsOfFile:@"examples/three-voice-arrangement.v4.json"]
            options:NSJSONReadingMutableContainers error:nil];
        for(NSMutableDictionary *node in v4[@"nodes"])if(const auto* spec=tri::nodeSpecForKey([node[@"nodeKey"] UTF8String])) {
            node[@"nodeVersion"]=@(spec->nodeVersion);node[@"stateVersion"]=@(spec->stateVersion);
        }
        // A completed decode belongs to the document the user dropped into,
        // even if the user opens a different workflow before it finishes.
        assert([editor dropFileURLs:@[dropURL] atPoint:NSMakePoint(180,180) clipMode:NO]);
        assert([editor adoptFile:v4]);
        NSDictionary *afterOpen=snapshot(editor);
        waitForJob(editor);
        assert([snapshot(editor) isEqual:afterOpen]);
        std::remove(dropPath.c_str());
        NSDictionary *opened=[editor valueForKey:@"document"];
        assert([opened[@"version"] isEqual:@4]);
        assert([opened[@"nodes"] count]==[v4[@"nodes"] count]);
        assert([opened[@"nodes"][1][@"nodeKey"] isEqual:@"soundnet.voice-arranger"]);
        assert([opened[@"edges"][1][@"sourcePort"] isEqual:@"high"]);

        // Inspector editing: every wired action must exist and reach the
        // document. These three regressions (dead enum popup, dead name field,
        // slider rounding 0-1 values) were all silent in the compiled build.
        {
            NSDictionary *doc=assets[@"graph"];
            NSUInteger oscIndex=0;
            for(NSUInteger i=0;i<[doc[@"nodes"] count];++i)
                if([doc[@"nodes"][i][@"nodeKey"] isEqual:@"soundnet.oscillator"]){oscIndex=i;break;}
            uint64_t uid=[doc[@"nodes"][oscIndex][@"id"] unsignedLongLongValue];
            [editor setValue:@(uid) forKey:@"selected"];
            [editor inspect];
            NSView *inspector=[editor valueForKey:@"inspector"];
            std::function<id(NSView*,Class,int)> find;
            find=[&](NSView *root,Class cls,int tag)->id{
                for(NSView *v in root.subviews) {
                    if([v isKindOfClass:cls]&&(tag<0||v.tag==tag))return v;
                    id deep=find(v,cls,tag);
                    if(deep)return deep;
                }
                return nil;
            };
            NSSlider *level=(NSSlider *)find(inspector,[NSSlider class],tri::parameterID("soundnet.oscillator","osc1.level")+100000);
            assert(level);
            level.doubleValue=0.42;
            [editor sliderChanged:level];
            double stored=[assets[@"graph"][@"nodes"][oscIndex][@"parameters"][@"osc1.level"] doubleValue];
            assert(std::abs(stored-0.42)<1e-5);
            NSPopUpButton *wave=(NSPopUpButton *)find(inspector,[NSPopUpButton class],tri::parameterID("soundnet.oscillator","osc1.wave"));
            assert(wave && wave.numberOfItems==4);
            [wave selectItemAtIndex:1];
            [editor choiceChanged:wave];
            assert([assets[@"graph"][@"nodes"][oscIndex][@"parameters"][@"osc1.wave"] intValue]==1);
            NSTextField *name=(NSTextField *)find(inspector,[NSTextField class],990001);
            assert(name);
            name.stringValue=@"Lead";
            [editor nameChanged:name];
            assert([assets[@"graph"][@"nodes"][oscIndex][@"name"] isEqual:@"Lead"]);

            // Disclosure controls change presentation only. Rebuilt controls
            // must retain and continue editing the same stable parameter IDs.
            NSDictionary *beforeGroups=snapshot(editor);
            NSButton *mix=(NSButton *)find(inspector,[NSButton class],990200+int(tri::ParamGroup::Mix));
            assert(mix && mix.action==@selector(toggleParameterGroup:));
            [editor toggleParameterGroup:mix];
            assert(!find(inspector,[NSSlider class],tri::parameterID("soundnet.oscillator","osc1.level")+100000));
            assert([snapshot(editor) isEqual:beforeGroups]);
            mix=(NSButton *)find(inspector,[NSButton class],990200+int(tri::ParamGroup::Mix));
            [editor toggleParameterGroup:mix];
            level=(NSSlider *)find(inspector,[NSSlider class],tri::parameterID("soundnet.oscillator","osc1.level")+100000);
            assert(level && std::abs(level.doubleValue-.42)<1e-5);
            level.doubleValue=.37;[editor sliderChanged:level];
            assert(std::abs([assets[@"graph"][@"nodes"][oscIndex][@"parameters"][@"osc1.level"] doubleValue]-.37)<1e-5);
            NSButton *tone=(NSButton *)find(inspector,[NSButton class],990200+int(tri::ParamGroup::Tone));
            [editor toggleParameterGroup:tone];
            assert(!find(inspector,[NSPopUpButton class],tri::parameterID("soundnet.oscillator","osc1.wave")));
            tone=(NSButton *)find(inspector,[NSButton class],990200+int(tri::ParamGroup::Tone));
            [editor toggleParameterGroup:tone];
            wave=(NSPopUpButton *)find(inspector,[NSPopUpButton class],tri::parameterID("soundnet.oscillator","osc1.wave"));
            assert(wave && wave.indexOfSelectedItem==1);
            [wave selectItemAtIndex:2];[editor choiceChanged:wave];
            assert([assets[@"graph"][@"nodes"][oscIndex][@"parameters"][@"osc1.wave"] intValue]==2);

            // Use a custom name that also exists in the translation dictionary.
            // It remains user data in both UI languages and in the document.
            name=(NSTextField *)find(inspector,[NSTextField class],990001);
            name.stringValue=@"振荡器";[editor nameChanged:name];
            NSDictionary *beforeLanguage=snapshot(editor);
            NSPopUpButton *language=[editor valueForKey:@"languagePopup"];
            [language selectItemAtIndex:1];[editor languageChanged:language];
            assert(SNIsEnglish());
            assert([((NSButton *)[editor valueForKey:@"playButton"]).title isEqualToString:@"Play"]);
            assert([snapshot(editor) isEqual:beforeLanguage]);
            name=(NSTextField *)find(inspector,[NSTextField class],990001);
            assert([name.stringValue isEqualToString:@"振荡器"]);
            wave=(NSPopUpButton *)find(inspector,[NSPopUpButton class],tri::parameterID("soundnet.oscillator","osc1.wave"));
            assert(wave.indexOfSelectedItem==2 && wave.numberOfItems==4);
            [language selectItemAtIndex:0];[editor languageChanged:language];
            assert(!SNIsEnglish());
            assert([((NSButton *)[editor valueForKey:@"playButton"]).title isEqualToString:@"播放"]);
            assert([snapshot(editor) isEqual:beforeLanguage]);

            NSView *canvas=[editor valueForKey:@"canvas"];
            NSRect originalCanvas=canvas.frame;
            [editor toggleLibrary:nil];
            assert(((NSScrollView *)[editor valueForKey:@"libraryScroll"]).hidden);
            assert(NSWidth(canvas.frame)>NSWidth(originalCanvas)+200);
            [editor toggleLibrary:nil];
            assert(!((NSScrollView *)[editor valueForKey:@"libraryScroll"]).hidden);
            assert(NSEqualRects(originalCanvas,canvas.frame));
            assert([snapshot(editor) isEqual:beforeLanguage]);

            NSDictionary *beforeStop=snapshot(editor);
            [editor stopGraph:nil];
            assert(!engine->processingEnabled());
            assert(![[editor valueForKey:@"processingActive"] boolValue]);
            assert([snapshot(editor) isEqual:beforeStop]);
            [editor playGraph:nil];
            assert(engine->processingEnabled());
            [editor insert:start];
            [editor stopGraph:nil];
            uint64_t startID=0;
            for(NSDictionary *node in assets[@"graph"][@"nodes"])
                if([node[@"nodeKey"] isEqual:@"soundnet.start"])startID=[node[@"id"] unsignedLongLongValue];
            assert(startID);
            [editor selectOnly:startID];[editor trigger:nil];
            assert(engine->processingEnabled());
        }

        // ---- score loop: an ABC score becomes an editable MIDI clip --------
        {
            NSMutableDictionary *document=[@{@"version":@4,@"projectId":@"local",
                @"nodes":[NSMutableArray new],@"edges":[NSMutableArray new]} mutableCopy];
            [editor setValue:document forKey:@"document"];
            NSMenuItem *aiItem=[NSMenuItem new];aiItem.tag=tri::nodeTypeForKey("soundnet.yue2");
            [editor insert:aiItem];
            NSMutableArray *nodes=[editor valueForKey:@"document"][@"nodes"];
            assert(nodes.count==1);
            NSMutableDictionary *aiNode=nodes.lastObject;
            uint64_t aiID=[aiNode[@"id"] unsignedLongLongValue];
            aiNode[@"texts"]=@{@"yue2.style":@"smoke test",
                               @"yue2.score":@"X:1\nT:Smoke\nM:4/4\nL:1/4\nQ:1/4=110\nK:G\nG A B c | d2 B2 |\n"};
            [editor selectOnly:aiID];
            [editor commit:nil];
            assert([assets[@"graph"][@"nodes"] count]==1);
            NSButton *import=buttonWithAction([editor valueForKey:@"inspector"],@selector(importScore:));
            assert(import&&import.target!=editor);[NSApp sendAction:import.action to:import.target from:import];
            nodes=[editor valueForKey:@"document"][@"nodes"];
            assert(nodes.count==2);
            NSDictionary *scoreClip=nodes.lastObject;
            assert([scoreClip[@"nodeKey"] isEqual:@"soundnet.midi-clip"]);
            assert([scoreClip[@"asset"][@"notes"] intValue]==6);
            assert([scoreClip[@"asset"][@"smf"] length]>0);
            assert([scoreClip[@"texts"][@"clip.score"] length]>0);
            assert([[editor activeIDs] count]==1);
            [editor undo:nil];
            assert([(NSArray *)[editor valueForKey:@"document"][@"nodes"] count]==1);
            [editor redo:nil];
            assert([(NSArray *)[editor valueForKey:@"document"][@"nodes"] count]==2);
        }

        NSData *serialized = [NSJSONSerialization dataWithJSONObject:assets options:0 error:nil];
        assert(triassets::validate([NSJSONSerialization JSONObjectWithData:serialized options:0 error:nil],nullptr));

        NSDictionary *example = [NSJSONSerialization JSONObjectWithData:
            [NSData dataWithContentsOfFile:@"examples/composition.json"]
            options:NSJSONReadingMutableContainers error:nil];
        for(NSMutableDictionary *node in example[@"nodes"])if(const auto* spec=tri::nodeSpecForKey([node[@"nodeKey"] UTF8String])) {node[@"nodeVersion"]=@(spec->nodeVersion);node[@"stateVersion"]=@(spec->stateVersion);}
        tri::GraphSpec decoded;
        assert(triassets::decodeGraph(example,decoded,nullptr));
        NSWindow *window = [[NSWindow alloc] initWithContentRect:editor.frame
            styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        window.releasedWhenClosed = NO;
        window.contentView = editor;
        [[NSFileManager defaultManager] createDirectoryAtPath:@"build/qa" withIntermediateDirectories:YES attributes:nil error:nil];
        verifyCableGesture(editor,clipDrop[@"asset"]);
        verifyAdaptiveZones(editor);
        verifyOuterTintAndInsertion(editor);
        [editor setValue:triassets::canonicalGraph(example,nil) forKey:@"document"];
        uint64_t previewID=0;
        for(NSDictionary *node in [editor valueForKey:@"document"][@"nodes"])
            if([node[@"nodeKey"] isEqual:@"soundnet.oscillator"]){previewID=[node[@"id"] unsignedLongLongValue];break;}
        assert(previewID);
        [editor selectOnly:previewID];
        [editor commit:nil];
        [editor fit:nil];
        [editor inspect];
        savePreview(editor,@"build/qa/instances.png");
        savePreview(editor,@"build/qa/instances-zh.png");
        savePreview(editor,@"build/qa/adaptive-canvas.png");
        SNSetEnglish(YES);
        [window setContentSize:NSMakeSize(1180,620)];
        [editor setFrameSize:NSMakeSize(1180,620)];
        [editor fit:nil];
        savePreview(editor,@"build/qa/instances-en.png");
        assert([editor adoptFile:v4]);
        [editor selectOnly:3];
        [window setContentSize:NSMakeSize(1180,840)];
        [editor setFrameSize:NSMakeSize(1180,840)];
        [editor fit:nil];
        savePreview(editor,@"build/qa/arranger-en.png");
        [window close];
        SNSetEnglish(originalEnglish);
        if(originalLanguage)[SNLanguagePreferences() setObject:originalLanguage forKey:@"SoundnetLanguage"];
        else [SNLanguagePreferences() removeObjectForKey:@"SoundnetLanguage"];
        if(ownPreferences)[SNLanguagePreferences() removePersistentDomainForName:preferenceSuite];
        [SNLanguagePreferences() synchronize];
        puts("PASS: native insertion/undo/drop/roundtrip, adaptive canvas placement, live region preview, cable preview and drop, grouped controls, bilingual UI/data preservation, sidebar sizing, stop/resume/trigger and layout previews.");
    }
}
