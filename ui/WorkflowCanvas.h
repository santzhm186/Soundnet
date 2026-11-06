#import "TriEditor.h"

// Node identities follow tri::Node in Engine.hpp (1..10, 0 = no selection).
typedef void (^TRINodeSelection)(int node);
@interface TRIWorkflowCanvas : NSView
- (instancetype)initWithFrame:(NSRect)frame read:(TRIRead)read write:(TRIWrite)write selected:(TRINodeSelection)selected;
@property(nonatomic,readonly) NSInteger selectedNode;
@property(nonatomic,readonly) NSInteger visibleNodeMask;
@property(nonatomic,readonly) CGFloat graphZoom;
- (void)selectNode:(NSInteger)node;
- (void)addNode:(NSInteger)node;
- (void)removeNode:(NSInteger)node;
- (void)refreshGraph;
- (void)zoomIn;
- (void)zoomOut;
- (void)fitGraph;
- (void)resetLayout;
@end
