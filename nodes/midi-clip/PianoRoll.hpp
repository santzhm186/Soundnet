#pragma once
#import <Cocoa/Cocoa.h>
#include "ControllerLane.hpp"
#include <cmath>

namespace tri::nodes::midi_clip {
inline constexpr double editTick=1.0/midiClipTicksPerBeat;
inline double snap(double value,double grid) { return grid>0?std::round(value/grid)*grid:value; }
inline ClipEvent controllerEvent(NSDictionary *row) {
    return {[row[@"beat"] doubleValue],uint8_t([row[@"status"] intValue]),
            uint8_t([row[@"data1"] intValue]),uint8_t([row[@"data2"] intValue])};
}
inline NSMutableDictionary *controllerRow(ClipEvent event) {
    return [@{@"beat":@(event.beat),@"status":@(event.status),
              @"data1":@(event.data1),@"data2":@(event.data2)} mutableCopy];
}
}

@interface SNNoteRollView : NSView
@property(nonatomic,strong) NSMutableArray<NSMutableDictionary *> *notes;
@property(nonatomic,strong) NSMutableArray<NSMutableDictionary *> *controllers;
@property(nonatomic) double lengthBeats;
@property(nonatomic) double grid;
@property(nonatomic) BOOL snapEnabled;
@property(nonatomic) BOOL pencil;
@property(nonatomic) NSInteger selectedIndex;
@property(nonatomic,readonly) NSIndexSet *selectedNotes;
@property(nonatomic) NSInteger selectedController;
@property(nonatomic) tri::nodes::midi_clip::ControllerLane lane;
@property(copy) void (^selectionChanged)(void);
@property(copy) NSString *(^noteName)(int);
- (void)fitNotes;
- (void)zoom:(double)factor;
- (void)deleteSelected;
- (void)updateSelectedNote:(NSString *)key value:(double)value;
- (void)updateSelectedControllerAt:(double)beat value:(int)value;
@end

@implementation SNNoteRollView {
    NSMutableIndexSet *_selection;
    NSMutableIndexSet *_selectionAtDown;
    NSArray<NSDictionary *> *_dragNotes;
    NSArray<NSDictionary *> *_noteClipboard;
    NSArray<NSMutableDictionary *> *_dragTargets;
    NSInteger _selectedIndex;
    NSInteger _activeVelocity;
    NSPoint _down;
    NSRect _marquee;
    double _firstBeat,_pixelsPerBeat;
    int _topPitch;
    enum class Drag { None, Move, Left, Right, Draw, Marquee, Velocity, Controller } _drag;
}

- (instancetype)initWithFrame:(NSRect)frame {
    if((self=[super initWithFrame:frame])) {
        _selection=[NSMutableIndexSet new];
        _controllers=[NSMutableArray new];
        _notes=[NSMutableArray new];
        _selectedIndex=_selectedController=_activeVelocity=-1;
        _lengthBeats=4;_grid=.25;_snapEnabled=YES;_topPitch=84;_pixelsPerBeat=96;
    }
    return self;
}
- (BOOL)isFlipped {return YES;}
- (BOOL)acceptsFirstResponder {return YES;}
- (double)left {return 54;}
- (double)top {return 24;}
- (double)rowHeight {return 14;}
- (double)laneTop {return std::max([self top]+[self rowHeight],NSHeight(self.bounds)-105);}
- (double)laneHeight {return std::max(1.0,NSHeight(self.bounds)-[self laneTop]-10);}
- (double)visibleBeats {return std::max(1.0,NSWidth(self.bounds)-[self left]-8)/_pixelsPerBeat;}
- (int)visibleRows {return std::max(1,int(std::ceil(([self laneTop]-[self top])/[self rowHeight])));}
- (double)beatAt:(NSPoint)p {return _firstBeat+(p.x-[self left])/_pixelsPerBeat;}
- (double)xAt:(double)beat {return [self left]+(beat-_firstBeat)*_pixelsPerBeat;}
- (int)pitchAt:(NSPoint)p {
    return std::clamp(_topPitch-int(std::floor((p.y-[self top])/[self rowHeight])),0,127);
}
- (double)snapPosition:(double)value {
    using namespace tri::nodes::midi_clip;
    return snap(value,_snapEnabled?_grid:0);
}
- (void)changed {
    if(_selectionChanged)_selectionChanged();
    [self setNeedsDisplay:YES];
}
- (void)clampViewport {
    _firstBeat=std::clamp(_firstBeat,0.0,std::max(0.0,_lengthBeats-[self visibleBeats]));
    _topPitch=std::clamp(_topPitch,std::min(127,[self visibleRows]-1),127);
}
- (void)setLengthBeats:(double)length {
    using namespace tri::nodes::midi_clip;
    if(!std::isfinite(length))return;
    _lengthBeats=std::clamp(length,editTick,65536.0);
    [self clampViewport];
    [self setNeedsDisplay:YES];
}
- (void)setGrid:(double)grid {_grid=std::max(.03125,grid);[self setNeedsDisplay:YES];}
- (void)setLane:(tri::nodes::midi_clip::ControllerLane)lane {
    _lane=lane;_selectedController=-1;[self changed];
}
- (void)setNotes:(NSMutableArray<NSMutableDictionary *> *)notes {
    _notes=[notes mutableCopy];[_selection removeAllIndexes];_selectedIndex=-1;
}
- (NSInteger)selectedIndex {return _selectedIndex;}
- (void)setSelectedIndex:(NSInteger)index {
    [_selection removeAllIndexes];
    _selectedIndex=index;
    if(index>=0 && index<(NSInteger)_notes.count)[_selection addIndex:NSUInteger(index)];
}
- (NSIndexSet *)selectedNotes {return [_selection copy];}
- (void)fitNotes {
    _firstBeat=0;
    _pixelsPerBeat=std::clamp((NSWidth(self.bounds)-[self left]-8)/std::max(.25,_lengthBeats),.01,400.0);
    if(_notes.count) {
        int lo=127,hi=0;
        for(NSDictionary *note in _notes) {
            const int pitch=[note[@"pitch"] intValue];
            lo=std::min(lo,pitch);hi=std::max(hi,pitch);
        }
        _topPitch=(hi+lo+[self visibleRows]-1)/2;
    }
    [self clampViewport];[self setNeedsDisplay:YES];
}
- (void)zoom:(double)factor {
    const double center=_firstBeat+[self visibleBeats]*.5;
    _pixelsPerBeat=std::clamp(_pixelsPerBeat*factor,.01,800.0);
    _firstBeat=center-[self visibleBeats]*.5;
    [self clampViewport];[self setNeedsDisplay:YES];
}
- (void)scrollWheel:(NSEvent *)event {
    if(event.modifierFlags&NSEventModifierFlagCommand) {
        [self zoom:std::exp(std::clamp(event.scrollingDeltaY*.04,-.7,.7))];
        return;
    }
    const bool horizontal=(event.modifierFlags&NSEventModifierFlagShift) ||
        std::abs(event.scrollingDeltaX)>std::abs(event.scrollingDeltaY);
    if(horizontal) {
        double delta=event.scrollingDeltaX;
        if(delta==0)delta=event.scrollingDeltaY;
        _firstBeat-=delta*(event.hasPreciseScrollingDeltas?1:16)/_pixelsPerBeat;
    } else if(event.scrollingDeltaY!=0) {
        const int rows=event.hasPreciseScrollingDeltas?
            std::max(1,int(std::round(std::abs(event.scrollingDeltaY)/[self rowHeight]))):3;
        _topPitch+=event.scrollingDeltaY>0?rows:-rows;
    }
    [self clampViewport];[self setNeedsDisplay:YES];
}
- (NSRect)rectForNote:(NSDictionary *)note {
    const double x=[self xAt:[note[@"start"] doubleValue]];
    const double y=[self top]+(_topPitch-[note[@"pitch"] intValue])*[self rowHeight]+1;
    return NSMakeRect(x,y,std::max(4.0,[note[@"length"] doubleValue]*_pixelsPerBeat),[self rowHeight]-2);
}
- (NSInteger)noteAt:(NSPoint)p {
    if(p.x<[self left] || p.y<[self top] || p.y>=[self laneTop])return -1;
    for(NSInteger i=NSInteger(_notes.count)-1;i>=0;--i)
        if(NSPointInRect(p,[self rectForNote:_notes[NSUInteger(i)]]))return i;
    return -1;
}
- (double)laneY:(int)value minimum:(int)minimum maximum:(int)maximum {
    return [self laneTop]+[self laneHeight]*(1.0-double(value-minimum)/double(maximum-minimum));
}
- (int)valueAt:(NSPoint)p minimum:(int)minimum maximum:(int)maximum {
    const double fraction=std::clamp(1-(p.y-[self laneTop])/[self laneHeight],0.0,1.0);
    return int(std::round(minimum+fraction*(maximum-minimum)));
}
- (NSInteger)controllerAt:(NSPoint)p {
    using namespace tri::nodes::midi_clip;
    for(NSInteger i=NSInteger(_controllers.count)-1;i>=0;--i) {
        const auto e=controllerEvent(_controllers[NSUInteger(i)]);
        if(!_lane.matches(e))continue;
        const double dx=p.x-[self xAt:e.beat];
        const double dy=p.y-[self laneY:_lane.value(e) minimum:_lane.minimum() maximum:_lane.maximum()];
        if(dx*dx+dy*dy<=64)return i;
    }
    return -1;
}
- (NSInteger)velocityAt:(NSPoint)p {
    NSInteger best=-1;
    double distance=64;
    for(NSUInteger i=0;i<_notes.count;++i) {
        NSDictionary *note=_notes[i];
        const double dx=p.x-[self xAt:[note[@"start"] doubleValue]];
        const double dy=p.y-[self laneY:[note[@"velocity"] intValue] minimum:0 maximum:127];
        const double score=dx*dx+dy*dy;
        if(score<=distance){best=NSInteger(i);distance=score;}
    }
    return best;
}
- (void)drawRect:(NSRect)dirty {
    using namespace tri::nodes::midi_clip;
    const double left=[self left],top=[self top],row=[self rowHeight],bottom=[self laneTop];
    const double width=NSWidth(self.bounds),height=NSHeight(self.bounds);
    [[NSColor colorWithWhite:.11 alpha:1] setFill];NSRectFill(dirty);
    [[NSColor colorWithWhite:.075 alpha:1] setFill];NSRectFill(NSMakeRect(0,bottom,width,height-bottom));
    NSDictionary *labels=@{NSFontAttributeName:[NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName:[NSColor colorWithWhite:.72 alpha:1]};
    static constexpr bool black[12]={false,true,false,true,false,false,true,false,true,false,true,false};
    for(int line=0;line<[self visibleRows] && line<=_topPitch;++line) {
        const int pitch=_topPitch-line;
        const double y=top+line*row;
        [[NSColor colorWithWhite:black[pitch%12]?.16:.27 alpha:1] setFill];
        NSRectFill(NSMakeRect(0,y,left-2,std::min(row,bottom-y)));
        if(!black[pitch%12]) {
            [[NSColor colorWithWhite:.135 alpha:1] setFill];
            NSRectFill(NSMakeRect(left,y,width-left,std::min(row,bottom-y)));
        }
        [[NSColor colorWithWhite:pitch%12==0?.32:.19 alpha:1] setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(0,y) toPoint:NSMakePoint(width,y)];
        if((pitch%12==0 || line==0) && _noteName)
            [_noteName(pitch) drawAtPoint:NSMakePoint(6,y+1) withAttributes:labels];
    }

    double step=_grid;
    while(step*_pixelsPerBeat<9)step*=2;
    const double first=std::ceil(_firstBeat/step)*step;
    for(double beat=first;beat<=_lengthBeats+1e-8 && [self xAt:beat]<width;beat+=step) {
        const double x=[self xAt:beat];
        const bool whole=std::abs(beat-std::round(beat))<1e-7;
        [[NSColor colorWithWhite:whole?.34:.21 alpha:1] setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(x,top) toPoint:NSMakePoint(x,height)];
        if(whole && (step>=1 || _pixelsPerBeat>=25))
            [[NSString stringWithFormat:@"%.0f",beat] drawAtPoint:NSMakePoint(x+3,5) withAttributes:labels];
    }
    [[NSColor colorWithWhite:.38 alpha:1] setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(left,bottom) toPoint:NSMakePoint(width,bottom)];
    for(int i=0;i<=2;++i) {
        const int min=_lane.minimum(),max=_lane.maximum();
        const int value=i==1 && _lane.kind==Lane::PitchBend?0:min+i*(max-min)/2;
        const double y=[self laneY:value minimum:min maximum:max];
        [[NSString stringWithFormat:@"%d",value] drawAtPoint:NSMakePoint(4,std::clamp(y-4,bottom+1,height-14)) withAttributes:labels];
        if(i==1)[NSBezierPath strokeLineFromPoint:NSMakePoint(left,y) toPoint:NSMakePoint(width,y)];
    }

    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSMakeRect(left,top,width-left,bottom-top));
    for(NSUInteger i=0;i<_notes.count;++i) {
        NSRect rect=[self rectForNote:_notes[i]];
        if(!NSIntersectsRect(rect,dirty))continue;
        const bool selected=[_selection containsIndex:i];
        [(selected?[NSColor colorWithRed:1 green:.76 blue:.36 alpha:1]:
                   [NSColor colorWithRed:.86 green:.54 blue:.24 alpha:.91]) setFill];
        [[NSBezierPath bezierPathWithRoundedRect:rect xRadius:2 yRadius:2] fill];
        if(selected) {[[NSColor colorWithWhite:1 alpha:.9] setStroke];NSFrameRect(rect);}
    }
    if(_drag==Drag::Marquee && !NSIsEmptyRect(_marquee)) {
        [[NSColor colorWithRed:.38 green:.65 blue:1 alpha:.2] setFill];NSRectFill(_marquee);
        [[NSColor colorWithRed:.38 green:.65 blue:1 alpha:.9] setStroke];NSFrameRect(_marquee);
    }
    [NSGraphicsContext restoreGraphicsState];

    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSMakeRect(left,bottom,width-left,height-bottom));
    if(_lane.kind==Lane::Velocity) {
        for(NSUInteger i=0;i<_notes.count;++i) {
            NSDictionary *note=_notes[i];
            const double x=[self xAt:[note[@"start"] doubleValue]];
            if(x<left-5 || x>width+5)continue;
            const double y=[self laneY:[note[@"velocity"] intValue] minimum:0 maximum:127];
            [( [_selection containsIndex:i] ? NSColor.systemOrangeColor :
                [NSColor colorWithRed:.72 green:.46 blue:.24 alpha:.8]) setFill];
            NSRectFill(NSMakeRect(x-1,y,3,height-10-y));
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(x-3,y-3,7,7)] fill];
        }
    } else {
        for(NSUInteger i=0;i<_controllers.count;++i) {
            const auto e=controllerEvent(_controllers[i]);
            if(!_lane.matches(e))continue;
            const double x=[self xAt:e.beat];
            if(x<left-5 || x>width+5)continue;
            const double y=[self laneY:_lane.value(e) minimum:_lane.minimum() maximum:_lane.maximum()];
            [(NSInteger(i)==_selectedController?NSColor.systemOrangeColor:NSColor.systemTealColor) setFill];
            NSRectFill(NSMakeRect(x-1,y,2,height-10-y));
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(x-4,y-4,8,8)] fill];
        }
    }
    [NSGraphicsContext restoreGraphicsState];
}

- (void)setController:(NSUInteger)index beat:(double)beat value:(int)value {
    using namespace tri::nodes::midi_clip;
    if(index>=_controllers.count || !std::isfinite(beat))return;
    _controllers[index]=controllerRow(_lane.event(std::clamp(beat,0.0,_lengthBeats),value));
}
- (void)updateSelectedControllerAt:(double)beat value:(int)value {
    if(_selectedController<0)return;
    [self setController:NSUInteger(_selectedController) beat:beat value:value];
    [self changed];
}
- (void)updateSelectedNote:(NSString *)key value:(double)value {
    using namespace tri::nodes::midi_clip;
    if(!std::isfinite(value))return;
    for(NSUInteger i=_selection.firstIndex;i!=NSNotFound;i=[_selection indexGreaterThanIndex:i]) {
        if(i>=_notes.count)break;
        NSMutableDictionary *note=_notes[i];
        const double start=[note[@"start"] doubleValue],length=[note[@"length"] doubleValue];
        double adjusted=value;
        if([key isEqual:@"start"])adjusted=std::clamp(value,0.0,std::max(0.0,_lengthBeats-length));
        else if([key isEqual:@"length"])adjusted=std::clamp(value,editTick,std::max(editTick,_lengthBeats-start));
        else if([key isEqual:@"velocity"])adjusted=std::clamp(std::round(value),1.0,127.0);
        else if([key isEqual:@"offVelocity"] || [key isEqual:@"pitch"])adjusted=std::clamp(std::round(value),0.0,127.0);
        else if([key isEqual:@"channel"])adjusted=std::clamp(std::round(value),0.0,15.0);
        note[key]=@(adjusted);
    }
    [self changed];
}
- (void)startNoteDrag:(Drag)mode point:(NSPoint)p {
    NSMutableArray *targets=[NSMutableArray new],*originals=[NSMutableArray new];
    for(NSUInteger i=_selection.firstIndex;i!=NSNotFound;i=[_selection indexGreaterThanIndex:i]) {
        if(i>=_notes.count)break;
        [targets addObject:_notes[i]];[originals addObject:[_notes[i] copy]];
    }
    _dragTargets=targets;_dragNotes=originals;_down=p;_drag=mode;
}
- (void)mouseDown:(NSEvent *)event {
    using namespace tri::nodes::midi_clip;
    [self.window makeFirstResponder:self];
    const NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];
    _down=p;_drag=Drag::None;
    const bool extend=(event.modifierFlags&NSEventModifierFlagShift)!=0;
    if(p.x<[self left] || p.y<[self top])return;
    if(p.y>=[self laneTop]) {
        if(_lane.kind==Lane::Velocity) {
            _activeVelocity=[self velocityAt:p];
            if(_activeVelocity>=0) {
                if(![_selection containsIndex:NSUInteger(_activeVelocity)])[self setSelectedIndex:_activeVelocity];
                _selectedIndex=_activeVelocity;
                [self startNoteDrag:Drag::Velocity point:p];
            }
        } else {
            const NSInteger hit=[self controllerAt:p];
            if(hit>=0)_selectedController=hit;
            else {
                const double beat=std::clamp([self snapPosition:[self beatAt:p]],0.0,_lengthBeats);
                const int value=[self valueAt:p minimum:_lane.minimum() maximum:_lane.maximum()];
                [_controllers addObject:controllerRow(_lane.event(beat,value))];
                _selectedController=NSInteger(_controllers.count)-1;
            }
            _drag=Drag::Controller;
        }
        [self changed];return;
    }
    _selectedController=-1;
    const NSInteger hit=[self noteAt:p];
    if(hit>=0) {
        if(extend && [_selection containsIndex:NSUInteger(hit)]) {
            [_selection removeIndex:NSUInteger(hit)];
            _selectedIndex=_selection.count?NSInteger(_selection.firstIndex):-1;
            [self changed];return;
        }
        if(extend)[_selection addIndex:NSUInteger(hit)];
        else if(![_selection containsIndex:NSUInteger(hit)])[self setSelectedIndex:hit];
        _selectedIndex=hit;
        const NSRect r=[self rectForNote:_notes[NSUInteger(hit)]];
        const double edge=std::min(7.0,NSWidth(r)*.25);
        Drag mode=Drag::Move;
        if(NSWidth(r)>=12) {
            if(p.x<NSMinX(r)+edge)mode=Drag::Left;
            else if(p.x>NSMaxX(r)-edge)mode=Drag::Right;
        }
        [self startNoteDrag:mode point:p];
    } else if(_pencil || event.clickCount==2) {
        const double start=std::clamp([self snapPosition:[self beatAt:p]],0.0,_lengthBeats-editTick);
        NSMutableDictionary *note=[@{@"pitch":@([self pitchAt:p]),@"start":@(start),
            @"length":@(std::min(_grid,_lengthBeats-start)),@"velocity":@100,@"channel":@(_lane.channel)} mutableCopy];
        [_notes addObject:note];[self setSelectedIndex:NSInteger(_notes.count)-1];
        [self startNoteDrag:Drag::Draw point:p];
    } else {
        if(!extend)[self setSelectedIndex:-1];
        _selectionAtDown=[_selection mutableCopy];_marquee=NSZeroRect;_drag=Drag::Marquee;
    }
    [self changed];
}
- (void)mouseDragged:(NSEvent *)event {
    using namespace tri::nodes::midi_clip;
    if(_drag==Drag::None)return;
    const NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];
    if(_drag==Drag::Controller) {
        [self updateSelectedControllerAt:[self snapPosition:[self beatAt:p]]
            value:[self valueAt:p minimum:_lane.minimum() maximum:_lane.maximum()]];
        return;
    }
    if(_drag==Drag::Marquee) {
        _marquee=NSMakeRect(std::min(_down.x,p.x),std::min(_down.y,p.y),std::abs(_down.x-p.x),std::abs(_down.y-p.y));
        _selection=[_selectionAtDown mutableCopy];
        for(NSUInteger i=0;i<_notes.count;++i)
            if(NSIntersectsRect(_marquee,[self rectForNote:_notes[i]]))[_selection addIndex:i];
        _selectedIndex=_selection.count?NSInteger(_selection.firstIndex):-1;
        [self changed];return;
    }
    if(_drag==Drag::Velocity) {
        const int delta=int(std::round((_down.y-p.y)*127/[self laneHeight]));
        for(NSUInteger i=0;i<_dragTargets.count;++i)
            _dragTargets[i][@"velocity"]=@(std::clamp([_dragNotes[i][@"velocity"] intValue]+delta,1,127));
        [self changed];return;
    }
    if(!_dragTargets.count)return;
    const double rawDelta=(p.x-_down.x)/_pixelsPerBeat;
    int pitchDelta=[self pitchAt:p]-[self pitchAt:_down];
    if(_drag==Drag::Draw) {
        NSMutableDictionary *note=_dragTargets[0];
        const double anchor=[_dragNotes[0][@"start"] doubleValue];
        const double edge=std::clamp([self snapPosition:anchor+rawDelta],0.0,_lengthBeats);
        note[@"start"]=@(std::min(anchor,edge));
        note[@"length"]=@(std::max(editTick,std::abs(edge-anchor)));
        note[@"pitch"]=@([self pitchAt:p]);
        [self changed];return;
    }
    if(_drag==Drag::Move && (event.modifierFlags&NSEventModifierFlagShift)) {
        if(std::abs(p.x-_down.x)>std::abs(p.y-_down.y))pitchDelta=0;
    }
    double low=-_lengthBeats,high=_lengthBeats;
    int lowPitch=-127,highPitch=127;
    for(NSDictionary *note in _dragNotes) {
        const double start=[note[@"start"] doubleValue],length=[note[@"length"] doubleValue];
        const int pitch=[note[@"pitch"] intValue];
        low=std::max(low,_drag==Drag::Right?editTick-length:-start);
        high=std::min(high,_drag==Drag::Left?length-editTick:_lengthBeats-start-length);
        lowPitch=std::max(lowPitch,-pitch);highPitch=std::min(highPitch,127-pitch);
    }
    NSMutableDictionary *anchor=_selectedIndex>=0 && _selectedIndex<NSInteger(_notes.count)?
        _notes[NSUInteger(_selectedIndex)]:nil;
    const NSUInteger anchorIndex=anchor?[_dragTargets indexOfObjectIdenticalTo:anchor]:NSNotFound;
    NSDictionary *original=anchorIndex==NSNotFound?_dragNotes[0]:_dragNotes[anchorIndex];
    double reference=[original[@"start"] doubleValue];
    if(_drag==Drag::Right)reference+=[original[@"length"] doubleValue];
    const bool vertical=_drag==Drag::Move && (event.modifierFlags&NSEventModifierFlagShift) &&
        std::abs(p.y-_down.y)>=std::abs(p.x-_down.x);
    const double snapped=rawDelta==0 || vertical?0:[self snapPosition:reference+rawDelta]-reference;
    const double delta=std::clamp(snapped,std::min(0.0,low),std::max(0.0,high));
    pitchDelta=std::clamp(pitchDelta,lowPitch,highPitch);
    for(NSUInteger i=0;i<_dragTargets.count;++i) {
        auto *note=_dragTargets[i];
        const double start=[_dragNotes[i][@"start"] doubleValue],length=[_dragNotes[i][@"length"] doubleValue];
        if(_drag!=Drag::Right)note[@"start"]=@(start+delta);
        if(_drag==Drag::Move)note[@"pitch"]=@([_dragNotes[i][@"pitch"] intValue]+pitchDelta);
        else note[@"length"]=@(length+(_drag==Drag::Left?-delta:delta));
    }
    [self changed];
}
- (void)mouseUp:(NSEvent *)event {
    (void)event;_drag=Drag::None;_dragTargets=nil;_dragNotes=nil;_selectionAtDown=nil;
    [self setNeedsDisplay:YES];
}
- (void)rightMouseDown:(NSEvent *)event {
    using namespace tri::nodes::midi_clip;
    const NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];
    if(p.x<[self left])return;
    if(p.y>=[self laneTop] && _lane.kind!=Lane::Velocity) {
        const NSInteger hit=[self controllerAt:p];
        if(hit>=0){[_controllers removeObjectAtIndex:NSUInteger(hit)];_selectedController=-1;[self changed];}
        return;
    }
    const NSInteger hit=[self noteAt:p];
    if(hit>=0){_selectedController=-1;[self setSelectedIndex:hit];[self deleteSelected];}
}
- (void)deleteSelected {
    using namespace tri::nodes::midi_clip;
    if(_lane.kind!=Lane::Velocity && _selectedController>=0) {
        if(_selectedController<(NSInteger)_controllers.count)
            [_controllers removeObjectAtIndex:NSUInteger(_selectedController)];
        _selectedController=-1;
    } else {
        [_notes removeObjectsAtIndexes:_selection];
        [self setSelectedIndex:-1];
    }
    [self changed];
}
- (void)keyDown:(NSEvent *)event {
    NSString *chars=event.charactersIgnoringModifiers.lowercaseString;
    if(event.modifierFlags&NSEventModifierFlagCommand) {
        if([chars isEqualToString:@"a"]) {
            [_selection addIndexesInRange:NSMakeRange(0,_notes.count)];
            _selectedIndex=_notes.count?0:-1;_selectedController=-1;[self changed];return;
        }
        if([chars isEqualToString:@"c"]) {
            _noteClipboard=[[NSArray alloc] initWithArray:[_notes objectsAtIndexes:_selection] copyItems:YES];
            return;
        }
        if([chars isEqualToString:@"v"]) {
            double end=0;
            for(NSDictionary *source in _noteClipboard)
                end=std::max(end,[source[@"start"] doubleValue]+[source[@"length"] doubleValue]);
            const double shift=std::min(_grid,std::max(0.0,_lengthBeats-end));
            if(shift>0 && _noteClipboard.count) {
                const NSUInteger first=_notes.count;
                [self setSelectedIndex:-1];
                for(NSDictionary *source in _noteClipboard) {
                    NSMutableDictionary *note=[source mutableCopy];
                    note[@"start"]=@([source[@"start"] doubleValue]+shift);
                    [_notes addObject:note];
                }
                [_selection addIndexesInRange:NSMakeRange(first,_noteClipboard.count)];
                _selectedIndex=NSInteger(first);[self changed];
            }
            return;
        }
    }
    if(event.keyCode==51 || event.keyCode==117)[self deleteSelected];
    else [super keyDown:event];
}
@end
