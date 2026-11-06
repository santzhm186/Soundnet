#import "WorkflowCanvas.h"
#include "../src/Engine.hpp"
#include "../app/PatchLayout.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr unsigned bg=0x141719, grid=0x22272a, majorGrid=0x2a3033, panel=0x252b2e, top=0x2f373a,
    edge=0x4a5559, textColor=0xe7eae7, soft=0xb4bdbd, dim=0x8b989b, selectedColor=0xece8d9,
    audioColor=0x82c3b2, midiColor=0xe7b87b, errorColor=0xe6aa92;
constexpr int firstNode=tri::MidiNode,lastNode=tri::layout::nodeCount;
constexpr CGFloat worldW=855, worldH=402;

NSColor *color(unsigned rgb, CGFloat alpha=1) {
    return [NSColor colorWithSRGBRed:((rgb>>16)&255)/255. green:((rgb>>8)&255)/255. blue:(rgb&255)/255. alpha:alpha];
}
void box(NSRect r, CGFloat radius,unsigned fill,unsigned stroke=0,CGFloat width=1) {
    NSBezierPath *p=[NSBezierPath bezierPathWithRoundedRect:r xRadius:radius yRadius:radius];
    [color(fill) setFill];[p fill];
    if(stroke){p.lineWidth=width;[color(stroke)setStroke];[p stroke];}
}
void drawText(NSString *s,NSRect r,CGFloat size,unsigned ink,bool bold=false,NSTextAlignment align=NSTextAlignmentLeft,bool mono=false) {
    NSMutableParagraphStyle *p=[NSMutableParagraphStyle new];p.alignment=align;p.lineBreakMode=NSLineBreakByTruncatingTail;
    NSFontWeight weight=bold?NSFontWeightSemibold:NSFontWeightRegular;
    NSFont *f=mono?[NSFont monospacedSystemFontOfSize:size weight:weight]:[NSFont systemFontOfSize:size weight:weight];
    [s drawInRect:r withAttributes:@{NSFontAttributeName:f,NSForegroundColorAttributeName:color(ink),NSParagraphStyleAttributeName:p}];
}
void polyLine(NSPoint a,NSPoint b,unsigned ink,CGFloat width=1){
    NSBezierPath *p=[NSBezierPath new];[p moveToPoint:a];[p lineToPoint:b];[color(ink)setStroke];p.lineWidth=width;[p stroke];
}
NSString *nameOf(int node, BOOL hosted) {
    if(node==tri::OutputNode) return hosted ? @"DAW 输出" : @"硬件输出";
    static NSArray<NSString*> *names;
    if(!names)names=@[@"",@"本地 MIDI",@"ARP",@"OSC 1",@"OSC 2",@"OSC 3",@"音频输入",@"混音器",@"均衡器",@"Delay",@"输出",@"和弦输入",@"和声走向",@"节奏速度",@"力度自动化",@"DAW MIDI",@"MIDI Clip",@"音符模型",@"MIDI 输出"];
    return node>=firstNode && node<=lastNode ? names[node] : @"";
}
// The upper line is the node title, the lower rows are actual ports. Audio and
// note paths have distinct colors and labels, including on the oscillator's two sides.
const std::array<NSRect,lastNode+1> positions={{
    {},NSMakeRect(12,151,78,57),NSMakeRect(118,151,78,78),NSMakeRect(230,15,111,76),
    NSMakeRect(230,111,111,76),NSMakeRect(230,207,111,76),NSMakeRect(230,309,111,57),
    NSMakeRect(381,122,99,117),NSMakeRect(512,151,90,57),NSMakeRect(634,151,91,57),NSMakeRect(759,151,83,57),
    NSMakeRect(20,430,128,76),NSMakeRect(180,430,128,76),NSMakeRect(340,430,128,57),NSMakeRect(500,430,138,96),
    NSMakeRect(12,50,103,57),NSMakeRect(12,280,103,57),NSMakeRect(125,279,97,57), NSMakeRect(742,319,100,57)
}};
bool validNode(int n){return n>=firstNode && n<=lastNode;}
bool sink(int n) { return n==tri::OutputNode || n==tri::MidiOutNode; }
bool osc(int n){return n>=tri::Osc1Node && n<=tri::Osc3Node;}
unsigned outColor(int n){return n==tri::ClockNode?0xb2a5e0:tri::noteSource(n)?midiColor:audioColor;}
unsigned inColor(int p){return tri::clockPort(p)?0xb2a5e0:(p<=tri::RouteOsc3In||p==tri::RouteChordIn||p==tri::RouteVelocityIn||p==tri::RouteModelIn||p==tri::RouteMidiOutIn)?midiColor:audioColor;}
int input0(int n) {
    if(n==tri::MidiOutNode)return tri::RouteMidiOutIn;
    if(n==tri::ClipNode)return tri::RouteClipClock;
    if(n==tri::ModelNode)return tri::RouteModelIn;
    if(n==tri::ChordNode)return tri::RouteChordIn;
    if(n==tri::ProgressNode)return tri::RouteProgressClock;
    if(n==tri::VelocityNode)return tri::RouteVelocityIn;
    if(n==tri::ArpNode)return tri::RouteArpIn;
    if(osc(n))return tri::RouteOsc1In+n-tri::Osc1Node;
    if(n==tri::MixNode)return tri::RouteMix1;
    if(n==tri::EQNode)return tri::RouteEQIn;
    if(n==tri::DelayNode)return tri::RouteDelayIn;
    if(n==tri::OutputNode)return tri::RouteOutputIn;
    return -1;
}
int inputs(int n){return n==tri::MixNode?4:(n==tri::ArpNode||n==tri::VelocityNode)?2:input0(n)>=0?1:0;}
int inputParam(int n,int slot){
    if(slot==1 && n==tri::ArpNode)return tri::RouteArpClock;
    if(slot==1 && n==tri::VelocityNode)return tri::RouteVelocityClock;
    return input0(n)+slot;
}
CGFloat dist(NSPoint a,NSPoint b){return std::hypot(a.x-b.x,a.y-b.y);}
struct Port { int node=0, param=-1; bool output=false; explicit operator bool() const{return node!=0;} };
enum class Gesture {none,pan,move,cable};
struct Curve {
    NSPoint a,b,c,d;
    Curve(NSPoint from,NSPoint to):a(from),d(to) {
        const CGFloat gap=to.x-from.x;
        CGFloat bend=gap>=0?std::clamp(gap*.52+std::min(55.,std::abs(to.y-from.y)*.13),18.,175.):
                              std::clamp(-gap*.36+std::abs(to.y-from.y)*.35+38.,44.,235.);
        b=NSMakePoint(a.x+bend,a.y); c=NSMakePoint(d.x-bend,d.y);
    }
    void stroke(unsigned ink,CGFloat width,CGFloat alpha=1,bool dashed=false) const {
        NSBezierPath *p=[NSBezierPath new];[p moveToPoint:a];[p curveToPoint:d controlPoint1:b controlPoint2:c];
        if(dashed){CGFloat dash[]={4,4};[p setLineDash:dash count:2 phase:0];}
        p.lineWidth=width;p.lineCapStyle=NSLineCapStyleRound;[color(ink,alpha)setStroke];[p stroke];
    }
    NSPoint at(CGFloat t) const {
        CGFloat s=1-t;
        return NSMakePoint(s*s*s*a.x+3*s*s*t*b.x+3*s*t*t*c.x+t*t*t*d.x,
                           s*s*s*a.y+3*s*s*t*b.y+3*s*t*t*c.y+t*t*t*d.y);
    }
    CGFloat distance(NSPoint point) const {
        CGFloat best=1e8;
        NSPoint last=a;
        for(int i=1;i<=32;++i) {
            NSPoint end=at(i/32.);
            CGFloat dx=end.x-last.x,dy=end.y-last.y;
            CGFloat t=std::clamp(((point.x-last.x)*dx+(point.y-last.y)*dy)/std::max(1e-9,dx*dx+dy*dy),0.,1.);
            best=std::min(best,dist(point,NSMakePoint(last.x+t*dx,last.y+t*dy))); last=end;
        }
        return best;
    }
};
}

@implementation TRIWorkflowCanvas {
    TRIRead _read;
    TRIWrite _write;
    TRINodeSelection _notify;
    tri::Routing _routes;
    std::array<NSRect,lastNode+1> _nodes;
    std::array<int,lastNode+1> _zOrder;
    std::array<int,3> _shapes;
    NSInteger _visibleNodeMask, _selectedNode;
    CGFloat _scale;
    NSPoint _origin, _startWorld, _startView, _startOrigin, _startNode, _mouseWorld, _menuPoint;
    Gesture _gesture;
    Port _startPort, _hover;
    int _movingNode,_selectedCable, _contextNode,_contextParam;
    BOOL _moved;
    NSTrackingArea *_tracking;
    NSString *_message;
    NSInteger _messageSerial;
}
-(instancetype)initWithFrame:(NSRect)r read:(TRIRead)read write:(TRIWrite)write selected:(TRINodeSelection)notify {
    if((self=[super initWithFrame:r])) {
        _read=[read copy];_write=[write copy];_notify=[notify copy];
        _nodes=positions;_routes=tri::defaultRouting;
        for(int n=1;n<=lastNode;n++)_zOrder[n]=n;
        _selectedNode=tri::Osc1Node;_selectedCable=-1;_scale=1;
        _visibleNodeMask=1023;
        self.toolTip=@"拖动端口连线；拖动节点标题移动；拖动空白处平移，双指捏合缩放。";
        [self setAccessibilityElement:YES];
        [self setAccessibilityRole:NSAccessibilityGroupRole];
        [self setAccessibilityLabel:@"Soundnet 工作流画布"];
        [self refreshGraph];[self fitGraph];
    }
    return self;
}
-(BOOL)isFlipped{return YES;}
-(BOOL)isOpaque{return YES;}
-(BOOL)acceptsFirstResponder{return YES;}
-(NSInteger)selectedNode{return _selectedNode;}
-(NSInteger)visibleNodeMask{return _visibleNodeMask;}
-(CGFloat)graphZoom{return _scale;}
-(BOOL)visible:(int)n{return validNode(n) && (_visibleNodeMask & (1<<(n-1)))!=0;}
-(void)selectNode:(NSInteger)n {
    if(n!=0 && ![self visible:(int)n])return;
    _selectedCable=-1;
    if(_selectedNode!=n){
        _selectedNode=n;
        if(_notify)_notify((int)n);
    }
    self.needsDisplay=YES;
}
-(void)refreshGraph {
    if(!_read)return;
    BOOL changed=NO;
    int mask=std::clamp((int)std::lround(_read(tri::layout::visibility)),0,(1<<lastNode)-1);
    if(mask!=_visibleNodeMask){_visibleNodeMask=mask;changed=YES;}
    for(int n=firstNode;n<=lastNode;++n){
        // Never pull a stale just-saved coordinate over a node being dragged.
        if(_gesture==Gesture::move && _movingNode==n)continue;
        CGFloat x=_read(tri::layout::x(n)),y=_read(tri::layout::y(n));
        NSPoint p=NSMakePoint(x>=0?x:positions[n].origin.x,y>=0?y:positions[n].origin.y);
        if(!NSEqualPoints(p,_nodes[n].origin)){_nodes[n].origin=p;changed=YES;}
    }
    for(int lane=0;lane<3;++lane){
        int shape=std::lround(_read(tri::Wave1+3*lane));
        if(_shapes[lane]!=shape){_shapes[lane]=shape;changed=YES;}
    }
    for(size_t i=0;i<_routes.size();++i){
        int p=tri::routeID(i);
        int src=static_cast<int>(std::round(_read(p)));
        if(_routes[i]!=src){_routes[i]=src;changed=YES;}
    }
    if(_selectedNode && ![self visible:(int)_selectedNode]){
        int fallback=0;
        for(int n=firstNode;n<=lastNode;++n)if([self visible:n]){fallback=n;break;}
        [self selectNode:fallback];
    }
    if(changed)self.needsDisplay=YES;
}
-(NSPoint)world:(NSPoint)p{return NSMakePoint((p.x-_origin.x)/_scale,(p.y-_origin.y)/_scale);}
-(NSPoint)eventPoint:(NSEvent*)e{return [self convertPoint:e.locationInWindow fromView:nil];}
-(void)fitGraph {
    NSRect world=NSZeroRect;
    for(int n=firstNode;n<=lastNode;++n)if([self visible:n])world=NSEqualRects(world,NSZeroRect)?_nodes[n]:NSUnionRect(world,_nodes[n]);
    if(NSIsEmptyRect(world))world=NSMakeRect(0,0,worldW,worldH);
    CGFloat w=std::max(100.,NSWidth(self.bounds)-40),h=std::max(80.,NSHeight(self.bounds)-74);
    _scale=std::clamp(std::min(w/NSWidth(world),h/NSHeight(world)),.16,1.4);
    _origin=NSMakePoint(NSMidX(self.bounds)-NSMidX(world)*_scale,NSMidY(self.bounds)-NSMidY(world)*_scale-7);
    self.needsDisplay=YES;
}
-(void)setZoom:(CGFloat)scale at:(NSPoint)at{
    NSPoint world=[self world:at];
    _scale=std::clamp(scale,.14,3.);
    _origin=NSMakePoint(at.x-world.x*_scale,at.y-world.y*_scale);self.needsDisplay=YES;
}
-(void)zoomIn{[self setZoom:_scale*1.2 at:NSMakePoint(NSMidX(self.bounds),NSMidY(self.bounds))];}
-(void)zoomOut{[self setZoom:_scale/1.2 at:NSMakePoint(NSMidX(self.bounds),NSMidY(self.bounds))];}
-(void)resetLayout{
    for(int n=firstNode;n<=lastNode;++n){_nodes[n]=positions[n];_write(tri::layout::x(n),-1);_write(tri::layout::y(n),-1);}
    [self fitGraph];
}
-(NSPoint)jack:(Port)p {
    NSRect r=_nodes[p.node];
    CGFloat y=NSMinY(r)+40;
    if(p.node==tri::MixNode && !p.output)y+=20*(p.param-tri::RouteMix1);
    if(p.node==tri::MixNode && p.output)y+=30;
    if(!p.output && ((p.node==tri::ArpNode && p.param==tri::RouteArpClock)||(p.node==tri::VelocityNode && p.param==tri::RouteVelocityClock)))y+=20;
    return NSMakePoint(p.output?NSMaxX(r):NSMinX(r),y);
}
-(Port)portAt:(NSPoint)p {
    CGFloat hit=std::clamp(9./_scale,6.,16.);
    for(int z=lastNode;z>=firstNode;--z){int n=_zOrder[z];if(![self visible:n])continue;
        if(!sink(n)){Port out{n,-1,true};if(dist(p,[self jack:out])<=hit)return out;}
        for(int j=0;j<inputs(n);j++){Port in{n,inputParam(n,j),false};if(dist(p,[self jack:in])<=hit)return in;}
        if(NSPointInRect(p,_nodes[n]))break;
    }
    return {};
}
-(int)nodeAt:(NSPoint)p {
    for(int z=lastNode;z>=firstNode;--z){int n=_zOrder[z];if([self visible:n]&&NSPointInRect(p,_nodes[n]))return n;}
    return 0;
}
-(int)cableAt:(NSPoint)p{
    CGFloat best=6./_scale;int found=-1;
    for(size_t i=0;i<_routes.size();++i){
        int id=tri::routeID(i), n=_routes[i], target=tri::routeTarget(id);
        if(![self visible:n]||![self visible:target])continue;
        Curve curve([self jack:Port{n,-1,true}],[self jack:Port{target,id,false}]);
        CGFloat d=curve.distance(p);if(d<best){best=d;found=id;}
    }
    return found;
}
-(void)message:(NSString*)s{
    _message=s;NSInteger serial=++_messageSerial;
    __weak TRIWorkflowCanvas *weak=self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC),dispatch_get_main_queue(),^{
        TRIWorkflowCanvas *canvas=weak;if(canvas && canvas->_messageSerial==serial){canvas->_message=nil;canvas.needsDisplay=YES;}
    });self.needsDisplay=YES;
}
-(BOOL)proposal:(Port)drop parameter:(int*)param source:(int*)source movedFrom:(int*)old {
    *param=-1;*source=0;*old=-1;
    if(!drop || !_startPort || ![self visible:drop.node])return NO;
    if(_startPort.output!=drop.output) {
        *param=_startPort.output?drop.param:_startPort.param;
        *source=_startPort.output?_startPort.node:drop.node;
    }else if(!drop.output && drop.param!=_startPort.param){
        *source=_routes[tri::routeIndex(_startPort.param)];
        if(![self visible:*source])return NO;
        *param=drop.param;*old=_startPort.param;
    }else return NO;
    tri::Routing candidate=_routes;
    if(*old>=0)candidate[tri::routeIndex(*old)]=tri::NoNode;
    return tri::canConnect(candidate,*param,*source);
}
-(void)writeRoute:(int)p source:(int)n{
    if(_routes[tri::routeIndex(p)]==n)return;
    _write(p,n);_routes[tri::routeIndex(p)]=n;
}
-(void)disconnect:(int)p{
    if(!tri::routeParam(p))return;
    [self writeRoute:p source:tri::NoNode];_selectedCable=-1;[self refreshGraph];self.needsDisplay=YES;
}
-(void)removeNode:(NSInteger)n{
    if(![self visible:(int)n])return;
    _write(tri::layout::beginRouting,0);
    for(size_t i=0;i<_routes.size();++i) {
        int p=tri::routeID(i);
        if(tri::routeTarget(p)==n || _routes[i]==n) [self writeRoute:p source:tri::NoNode];
    }
    _write(tri::layout::commitRouting,0);
    _write(tri::layout::visibility,_visibleNodeMask & ~(1<<(n-1)));
    [self refreshGraph];self.needsDisplay=YES;
}
-(void)addNode:(NSInteger)n{[self add:n at:[self world:NSMakePoint(NSMidX(self.bounds),NSMidY(self.bounds))]];}
-(void)add:(NSInteger)n at:(NSPoint)p{
    if(!validNode((int)n))return;
    if([self visible:(int)n]){[self selectNode:n];return;}
    _write(tri::layout::x((int)n),std::max(0.,p.x-NSWidth(_nodes[n])*.5));
    _write(tri::layout::y((int)n),std::max(0.,p.y-12.));
    _write(tri::layout::visibility,_visibleNodeMask|(1<<(n-1)));[self refreshGraph];[self selectNode:n];self.needsDisplay=YES;
}
-(void)drawSocket:(Port)p connected:(BOOL)used{
    NSPoint c=[self jack:p];unsigned tint=p.output?outColor(p.node):inColor(p.param);
    BOOL hot=(_hover.node==p.node && _hover.param==p.param && _hover.output==p.output);
    if(hot)box(NSMakeRect(c.x-7.6,c.y-7.6,15.2,15.2),7.6,bg,selectedColor,1.);
    box(NSMakeRect(c.x-4.4,c.y-4.4,8.8,8.8),4.4,used?tint:panel,tint,1.45);
}
-(void)drawNode:(int)n{
    NSRect r=_nodes[n];bool selected=n==_selectedNode;
    box(NSOffsetRect(r,0,2.4),6,0x0e1012);box(r,6,panel,selected?selectedColor:edge,selected?1.8:1);
    NSRect title=NSMakeRect(r.origin.x+1,r.origin.y+1,NSWidth(r)-2,24);
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r,1,1) xRadius:5 yRadius:5]addClip];
    [color(top)setFill];NSRectFill(title);[NSGraphicsContext restoreGraphicsState];
    drawText(nameOf(n,_hosted),NSMakeRect(NSMinX(r)+10,NSMinY(r)+4,NSWidth(r)-16,19),12.1,textColor,true);
    for(int j=0;j<inputs(n);++j){
        int param=inputParam(n,j); Port port{n,param,false}; NSPoint pt=[self jack:port];
        [self drawSocket:port connected:_routes[tri::routeIndex(param)]!=0];
        NSString *s=tri::clockPort(param)?@"速度":n==tri::MixNode?[NSString stringWithFormat:@"IN %d",j+1]:inColor(param)==midiColor?@"音符":@"音频";
        drawText(s,NSMakeRect(pt.x+9,pt.y-6.6,45,16),10,soft);
    }
    if(!sink(n)){
        Port out{n,-1,true}; NSPoint pt=[self jack:out];
        BOOL used=std::find(_routes.begin(),_routes.end(),n)!=_routes.end();
        [self drawSocket:out connected:used];
        NSString *s=n==tri::ClockNode?@"速度":tri::noteSource(n)?@"音符":@"音频";
        if(n==tri::EQNode||n==tri::DelayNode||n==tri::MixNode)s=@"OUT";
        drawText(s,NSMakeRect(pt.x-49,pt.y-6.6,39,16),10,soft,false,NSTextAlignmentRight);
    }
    if(osc(n)){
        CGFloat x=NSMinX(r)+10,y=NSMaxY(r)-13;
        int wave=std::clamp(_shapes[n-tri::Osc1Node],0,3);
        NSBezierPath *path=[NSBezierPath new];
        for(int i=0;i<=56;++i){
            CGFloat u=i/56.,phase=std::fmod(u*2+.05,1.),amp=wave==0?std::sin(2*M_PI*phase):wave==1?(phase<.5?1:-1):wave==2?phase*2-1:1-4*std::abs(phase-.5);
            NSPoint q=NSMakePoint(x+u*34,y-4.*amp);if(!i)[path moveToPoint:q];else[path lineToPoint:q];
        }
        path.lineWidth=1.15;[color(audioColor)setStroke];[path stroke];
        static NSArray *titles=@[@"SINE",@"SQUARE",@"SAW",@"TRI"];
        drawText(titles[wave],NSMakeRect(x+41,y-6.5,NSWidth(r)-57,14),9.5,dim,false,NSTextAlignmentRight,true);
    }
}
-(void)drawRect:(NSRect)dirty{
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(self.bounds);
    [color(bg)setFill];NSRectFill(self.bounds);
    CGFloat step=16*_scale;
    if(step<8)step*=4;
    CGFloat gridX=std::fmod(_origin.x,step),gridY=std::fmod(_origin.y,step);
    for(CGFloat x=gridX;x<NSWidth(self.bounds);x+=step)polyLine(NSMakePoint(x,0),NSMakePoint(x,NSHeight(self.bounds)),grid,.55);
    for(CGFloat y=gridY;y<NSHeight(self.bounds);y+=step)polyLine(NSMakePoint(0,y),NSMakePoint(NSWidth(self.bounds),y),grid,.55);
    step*=4;
    for(CGFloat x=std::fmod(_origin.x,step);x<NSWidth(self.bounds);x+=step)polyLine(NSMakePoint(x,0),NSMakePoint(x,NSHeight(self.bounds)),majorGrid,.55);
    for(CGFloat y=std::fmod(_origin.y,step);y<NSHeight(self.bounds);y+=step)polyLine(NSMakePoint(0,y),NSMakePoint(NSWidth(self.bounds),y),majorGrid,.55);
    [NSGraphicsContext saveGraphicsState];
    NSAffineTransform *matrix=[NSAffineTransform transform];[matrix translateXBy:_origin.x yBy:_origin.y];[matrix scaleBy:_scale];[matrix concat];
    for(size_t i=0;i<_routes.size();++i){
        int p=tri::routeID(i), src=_routes[i], target=tri::routeTarget(p);
        if(![self visible:src]||![self visible:target])continue;
        Curve curve([self jack:Port{src,-1,true}],[self jack:Port{target,p,false}]);
        bool muted=_gesture==Gesture::cable && !_startPort.output && p==_startPort.param;
        curve.stroke(0x0b0e0f,p==_selectedCable?5.7:4.5,.85);
        curve.stroke(p==_selectedCable?selectedColor:outColor(src),p==_selectedCable?3.:2.1,muted?.28:.93);
    }
    for(int z=firstNode;z<=lastNode;++z){int n=_zOrder[z];if([self visible:n])[self drawNode:n];}
    if(_gesture==Gesture::cable){
        NSPoint j=[self jack:_startPort],end=_hover?[self jack:_hover]:_mouseWorld;
        int param,src,old;bool ok=[self proposal:_hover parameter:&param source:&src movedFrom:&old];
        unsigned tint=_hover && !ok?errorColor:_startPort.output?outColor(_startPort.node):inColor(_startPort.param);
        Curve(_startPort.output?j:end,_startPort.output?end:j).stroke(tint,2.7,1,!ok);
    }
    [NSGraphicsContext restoreGraphicsState];
    NSString *hint=_message ?: (_gesture==Gesture::cable?@"松开到兼容端口以连接  ·  Esc 取消":@"拖动端口连线  ·  拖动画布平移  ·  右键管理节点与连接");
    NSRect info=NSMakeRect(11,NSHeight(self.bounds)-27,315,20);
    box(info,4,0x1b2022);
    drawText(hint,NSOffsetRect(NSInsetRect(info,5,1),0,1),10.5,_message?textColor:dim);
    drawText([NSString stringWithFormat:@"%.0f%%",_scale*100],NSMakeRect(NSWidth(self.bounds)-62,NSHeight(self.bounds)-25,50,17),10.5,dim,false,NSTextAlignmentRight,true);
    [color(edge,.75)setStroke];[NSBezierPath strokeRect:NSInsetRect(self.bounds,.5,.5)];
    [NSGraphicsContext restoreGraphicsState];
}
-(void)updateTrackingAreas {
    [super updateTrackingAreas];
    if(_tracking)[self removeTrackingArea:_tracking];
    _tracking=[[NSTrackingArea alloc]initWithRect:NSZeroRect options:
        NSTrackingMouseMoved|NSTrackingMouseEnteredAndExited|NSTrackingActiveInKeyWindow|NSTrackingInVisibleRect owner:self userInfo:nil];
    [self addTrackingArea:_tracking];
}
-(void)mouseMoved:(NSEvent*)e{
    _mouseWorld=[self world:[self eventPoint:e]];Port p=[self portAt:_mouseWorld];
    if(p.node!=_hover.node||p.output!=_hover.output||p.param!=_hover.param){_hover=p;self.needsDisplay=YES;}
    if(p)[NSCursor.crosshairCursor set];else if([self nodeAt:_mouseWorld])[NSCursor.arrowCursor set];else[NSCursor.openHandCursor set];
}
-(void)mouseExited:(NSEvent*)e{_hover={};self.needsDisplay=YES;}
-(void)mouseDown:(NSEvent*)e{
    [self.window makeFirstResponder:self];[self refreshGraph];
    _startView=[self eventPoint:e];_startWorld=[self world:_startView];_mouseWorld=_startWorld;_moved=NO;
    _startOrigin=_origin;_hover={};_startPort=[self portAt:_startWorld];
    if(_startPort && (e.modifierFlags&NSEventModifierFlagOption) && !_startPort.output){
        [self disconnect:_startPort.param];return;
    }
    if(_startPort){_gesture=Gesture::cable;self.needsDisplay=YES;return;}
    int n=[self nodeAt:_startWorld];
    if(n){_gesture=Gesture::move;_movingNode=n;_startNode=_nodes[n].origin;
        auto it=std::find(_zOrder.begin()+1,_zOrder.end(),n);
        std::rotate(it,it+1,_zOrder.end());[self selectNode:n];
    }else{_gesture=Gesture::pan;_selectedCable=[self cableAt:_startWorld];[NSCursor.closedHandCursor set];}
    self.needsDisplay=YES;
}
-(void)mouseDragged:(NSEvent*)e{
    NSPoint p=[self eventPoint:e];_mouseWorld=[self world:p];
    if(dist(p,_startView)>2)_moved=YES;
    switch(_gesture){
        case Gesture::move:{
            CGFloat x=std::max(0.,_startNode.x+_mouseWorld.x-_startWorld.x);
            CGFloat y=std::max(0.,_startNode.y+_mouseWorld.y-_startWorld.y);
            _nodes[_movingNode].origin=NSMakePoint(x,y);break;
        }
        case Gesture::pan:_origin=NSMakePoint(_startOrigin.x+p.x-_startView.x,_startOrigin.y+p.y-_startView.y);break;
        case Gesture::cable:_hover=[self portAt:_mouseWorld];break;
        default:return;
    }
    self.needsDisplay=YES;
}
-(void)mouseUp:(NSEvent*)e{
    const Gesture mode=_gesture;
    NSPoint p=[self world:[self eventPoint:e]];
    if(mode==Gesture::move && _moved){
        NSPoint point=_nodes[_movingNode].origin;
        _write(tri::layout::x(_movingNode),point.x);_write(tri::layout::y(_movingNode),point.y);
    }else if(mode==Gesture::cable && _moved){
        Port drop=[self portAt:p];int param,src,old;
        if([self proposal:drop parameter:&param source:&src movedFrom:&old]){
            if(old>=0)_write(tri::layout::beginRouting,0);
            if(old>=0)[self writeRoute:old source:tri::NoNode];
            [self writeRoute:param source:src];
            if(old>=0)_write(tri::layout::commitRouting,0);
            _selectedCable=param;
        }else if(!drop && !_startPort.output && _routes[tri::routeIndex(_startPort.param)]!=0 && ![self nodeAt:p]){
            [self disconnect:_startPort.param];[self message:@"已断开输入连接"];
        }else if(drop){[self message:@"请连接相同类型的端口，并避免形成回路"];}
    }
    _gesture=Gesture::none;_movingNode=0;_startPort={};[self refreshGraph];
    [self mouseMoved:e];self.needsDisplay=YES;
}
-(void)otherMouseDown:(NSEvent*)e{
    _startView=[self eventPoint:e];_startOrigin=_origin;_gesture=Gesture::pan;_moved=NO;
    [NSCursor.closedHandCursor set];
}
-(void)otherMouseDragged:(NSEvent*)e{[self mouseDragged:e];}
-(void)otherMouseUp:(NSEvent*)e{[self mouseUp:e];}
-(void)scrollWheel:(NSEvent*)e{
    CGFloat f=e.hasPreciseScrollingDeltas?1:12;
    if(e.modifierFlags&NSEventModifierFlagOption)[self setZoom:_scale*std::exp(e.scrollingDeltaY*.007*f) at:[self eventPoint:e]];
    else { _origin.x+=e.scrollingDeltaX*f;_origin.y+=e.scrollingDeltaY*f;self.needsDisplay=YES; }
}
-(void)magnifyWithEvent:(NSEvent*)e{[self setZoom:_scale*(1+e.magnification) at:[self eventPoint:e]];}
-(void)keyDown:(NSEvent*)e{
    if(e.keyCode==53 && _gesture!=Gesture::none){
        if(_gesture==Gesture::move)_nodes[_movingNode].origin=_startNode;
        if(_gesture==Gesture::pan)_origin=_startOrigin;
        _gesture=Gesture::none;_hover={};_startPort={};self.needsDisplay=YES;return;
    }
    if(e.keyCode==51 || e.keyCode==117){
        if(tri::routeParam(_selectedCable))[self disconnect:_selectedCable];else if(_selectedNode)[self removeNode:_selectedNode];
        return;
    }
    [super keyDown:e];
}
-(NSMenuItem*)entry:(NSMenu*)menu title:(NSString*)title action:(SEL)action {
    NSMenuItem *item=[menu addItemWithTitle:title action:action keyEquivalent:@""];item.target=self;return item;
}
-(NSMenu*)menuForEvent:(NSEvent*)e{
    [self refreshGraph];_menuPoint=[self world:[self eventPoint:e]];
    Port p=[self portAt:_menuPoint];_contextNode=p?p.node:[self nodeAt:_menuPoint];
    _contextParam=p && !p.output?p.param:(_contextNode? -1:[self cableAt:_menuPoint]);
    NSMenu *menu=[NSMenu new];
    if(tri::routeParam(_contextParam) && _routes[tri::routeIndex(_contextParam)]!=tri::NoNode){
        [self entry:menu title:@"断开这条连接" action:@selector(menuDisconnect:)];
    }
    if(_contextNode) {
        [self selectNode:_contextNode];
        [self entry:menu title:[@"移除 " stringByAppendingString:nameOf(_contextNode,_hosted)] action:@selector(menuRemove:)];
    }else{
        NSMenu *add=[NSMenu new];
        for(int n=firstNode;n<=lastNode;++n)if(![self visible:n])
            [self entry:add title:nameOf(n,_hosted) action:@selector(menuAdd:)].tag=n;
        if(add.numberOfItems){NSMenuItem *item=[menu addItemWithTitle:@"添加节点" action:nil keyEquivalent:@""];item.submenu=add;}
        if(menu.numberOfItems)[menu addItem:NSMenuItem.separatorItem];
        [self entry:menu title:@"适配画布" action:@selector(menuFit:)];
        [self entry:menu title:@"重排节点" action:@selector(menuReset:)];
    }
    return menu;
}
-(void)menuDisconnect:(id)sender{[self disconnect:_contextParam];}
-(void)menuRemove:(id)sender{[self removeNode:_contextNode];}
-(void)menuAdd:(NSMenuItem*)sender{[self add:sender.tag at:_menuPoint];}
-(void)menuFit:(id)sender{[self fitGraph];}
-(void)menuReset:(id)sender{[self resetLayout];}
@end
