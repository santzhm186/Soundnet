#import "../ui/TriEditor.h"
#import "../ui/WorkflowFile.hpp"
int main(int argc,char **argv) {
    if(argc<2) return 1;
    @autoreleasepool {
        [NSApplication sharedApplication];
        tri::Engine engine;
        auto layout=tri::layout::defaults();
        const auto *positions=&layout;
        tri::Engine *p=&engine;
        p->set(tri::ArpOn,1); p->set(tri::Pattern,1); p->set(tri::ArpRepeat,2);
        p->set(tri::Osc1Attack,.008f); p->set(tri::Osc2Sustain,.45f); p->set(tri::Osc3Release,.9f);
        TRIRead read=^(int id){ return tri::layout::contains(id)?(*positions)[id-tri::layout::first]:p->get(id); };
        if(argc==3) {
            p->set(tri::RouteOsc1In,tri::MidiNode);
            NSDictionary *file=ternaryfile::snapshot(read);
            NSData *data=[NSJSONSerialization dataWithJSONObject:file options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:nil];
            id parsed=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
            if(!ternaryfile::validate(parsed,nullptr) || ![data writeToFile:@(argv[2]) atomically:YES]) return 2;
        }
        NSView *view=TRICreateEditor(read,^(int,float){},nil,^BOOL{return NO;},^(BOOL){},^{return @"48 kHz  ·  MIDI";});
        NSWindow *window=[[NSWindow alloc] initWithContentRect:view.frame styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        window.contentView=view; [window display]; [view layoutSubtreeIfNeeded];
        NSBitmapImageRep *rep=[view bitmapImageRepForCachingDisplayInRect:view.bounds];
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
        NSData *png=[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        return ![png writeToFile:@(argv[1]) atomically:YES];
    }
}
