#import <Cocoa/Cocoa.h>
#import "../ui/SNLocalization.h"
#include "../src/NodeRegistry.hpp"
#include <cassert>
#include <cstdio>

@interface NSView (SoundnetLibraryTestActions)
- (void)setSearchQuery:(NSString *)query;
- (void)toggleCategory:(NSString *)category;
- (void)expandAll:(id)sender;
- (void)collapseAll:(id)sender;
@end

@interface SNLibraryInsertionProbe : NSObject
@property int insertedType;
- (void)insertFromLibrary:(int)type;
@end
@implementation SNLibraryInsertionProbe
- (void)insertFromLibrary:(int)type {self.insertedType=type;}
@end

static NSArray *items(NSView *library) {
    return [[library valueForKey:@"rows"] filteredArrayUsingPredicate:
        [NSPredicate predicateWithFormat:@"kind == 'item'"]];
}

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSUserDefaults *defaults=NSUserDefaults.standardUserDefaults;
        id saved=[defaults objectForKey:@"SoundnetCollapsedNodeCategories"];
        [defaults removeObjectForKey:@"SoundnetCollapsedNodeCategories"];
        NSView *library=[[NSClassFromString(@"SNNodeLibrary") alloc]initWithFrame:NSMakeRect(0,0,216,600)];
        assert(library);
        const NSUInteger total=tri::nodeCatalog().size();
        assert(items(library).count==total);
        NSArray *rows=[library valueForKey:@"rows"];
        NSArray *sections=[rows filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"kind == 'section'"]];
        assert([[sections valueForKey:@"section"] isEqual:(@[@"创作节点",@"辅助节点"])]);
        NSArray *headers=[rows filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"kind == 'header'"]];
        assert([[headers valueForKey:@"category"] isEqual:(@[@"音符",@"发声",@"声音处理",@"走带与控制",@"工程与 I/O"])]);
        const NSUInteger sourceCount=[[items(library) filteredArrayUsingPredicate:
            [NSPredicate predicateWithFormat:@"category == %@",@"发声"]] count];
        assert(sourceCount>0);

        [library toggleCategory:@"发声"];
        assert(items(library).count==total-sourceCount);
        // Filtering reveals matches in a collapsed category without changing
        // the user's category state. Both languages work in either UI language.
        [library setSearchQuery:@"oscillator"];
        assert(items(library).count>0);
        for(NSDictionary *row in items(library))assert([row[@"category"] isEqual:@"发声"]);
        [library setSearchQuery:@"振荡器"];
        assert(items(library).count>0);
        [library setSearchQuery:@"soundnet.oscillator"];
        assert(items(library).count==1);
        [library setSearchQuery:@"Project Settings"];
        assert(items(library).count==1 && [items(library).firstObject[@"type"] intValue]==tri::nodeTypeForKey("soundnet.project-settings"));
        [library setSearchQuery:@"Timecode"];
        assert(items(library).count==1 && [items(library).firstObject[@"type"] intValue]==tri::nodeTypeForKey("soundnet.timecode"));
        [library setSearchQuery:@"  SOUND SOURCES oscillator  "];
        assert(items(library).count>0);
        [library setSearchQuery:@"definitely-no-such-node"];
        assert(items(library).count==0);
        assert([[library valueForKey:@"rows"] count]==0);
        [library setSearchQuery:@""];
        assert(items(library).count==total-sourceCount);

        [library collapseAll:nil];
        assert(items(library).count==0);
        assert([[library valueForKey:@"rows"] count]>0);
        [library setSearchQuery:@"辅助节点"];
        assert(items(library).count>0);
        [library expandAll:nil]; // Search mode must not overwrite saved state.
        [library setSearchQuery:@""];
        assert(items(library).count==0);
        [library expandAll:nil];
        assert(items(library).count==total);
        [library toggleCategory:@"工程与 I/O"];
        NSView *second=[[NSClassFromString(@"SNNodeLibrary") alloc]initWithFrame:NSMakeRect(0,0,216,600)];
        assert(items(second).count==items(library).count);

        NSSearchField *search=(NSSearchField *)[library viewWithTag:990101];
        assert([search isKindOfClass:NSSearchField.class]);
        assert(search.delegate && search.sendsSearchStringImmediately);
        [library setFrameSize:NSMakeSize(250,700)];
        assert(NSWidth(search.frame)==234);
        assert(NSHeight(library.frame)>=700);

        // Clicking a filtered result still carries the descriptor type through
        // the insertion callback; filtering must not confuse row indices.
        [library setSearchQuery:@"soundnet.oscillator"];
        SNLibraryInsertionProbe *probe=[SNLibraryInsertionProbe new];
        [library setValue:probe forKey:@"editor"];
        NSWindow *window=[[NSWindow alloc]initWithContentRect:NSMakeRect(0,0,250,700)
            styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        window.releasedWhenClosed=NO;window.contentView=library;
        NSDictionary *row=items(library).firstObject;
        NSPoint local=NSMakePoint(40,[row[@"y"] doubleValue]+14);
        NSPoint location=[library convertPoint:local toView:nil];
        NSEvent *down=[NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:location modifierFlags:0
            timestamp:0 windowNumber:window.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1];
        NSEvent *up=[NSEvent mouseEventWithType:NSEventTypeLeftMouseUp location:location modifierFlags:0
            timestamp:.1 windowNumber:window.windowNumber context:nil eventNumber:2 clickCount:1 pressure:0];
        [library mouseDown:down];[library mouseUp:up];
        assert(probe.insertedType==tri::nodeTypeID("soundnet.oscillator"));
        [window close];
        if(saved)[defaults setObject:saved forKey:@"SoundnetCollapsedNodeCategories"];
        else [defaults removeObjectForKey:@"SoundnetCollapsedNodeCategories"];
        puts("PASS: bilingual node search, category collapse, search-state preservation, empty results, saved disclosure state and resizing.");
    }
}
