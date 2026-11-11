#import <Cocoa/Cocoa.h>
typedef float (^TRIRead)(int param);
typedef void (^TRIWrite)(int param, float value);
typedef void (^TRIMIDI)(unsigned char status, unsigned char d1, unsigned char d2);
typedef BOOL (^TRIInputRead)(void);
typedef void (^TRIInputWrite)(BOOL enabled);
typedef NSString * (^TRIStatus)(void);
typedef NSDictionary * (^TRIAssetsRead)(void);
typedef BOOL (^TRIAssetsWrite)(NSDictionary *assets);
typedef BOOL (^TRIProcessingRead)(void);
typedef void (^TRIProcessingWrite)(BOOL enabled);
// Node-level stop for CapTask nodes: stops playback of one instance without
// touching the transport or any project parameter. Optional; AU passes nil.
typedef void (^TRINodeStop)(uint64_t nodeID);
typedef NSDictionary * (^TRIEnvironmentRead)(void);
typedef NSDictionary * (^TRIMidiCapture)(NSDictionary *command);
// Fixed 1144 x 650 points. Pass nil for input switches in AU (host routes audio).
// status is a short connection/engine status, e.g. @"CoreMIDI · 2 sources".
NSView *TRICreateEditor(TRIRead read, TRIWrite write, TRIMIDI midi, TRIInputRead inputRead, TRIInputWrite inputWrite, TRIStatus status,
                        TRIAssetsRead assetsRead = nullptr, TRIAssetsWrite assetsWrite = nullptr,
                        TRIProcessingRead processingRead = nullptr, TRIProcessingWrite processingWrite = nullptr,
                        TRINodeStop nodeStop = nullptr, TRIEnvironmentRead environmentRead = nullptr,
                        TRIMidiCapture midiCapture = nullptr);
BOOL TRIOpenEditorProject(NSView *view, NSURL *url);
void TRIFlushEditorAutosave(NSView *view);
typedef void (^TRINodeLibraryRefresh)(void (^completed)(NSString *message));
void TRISetNodeLibraryRefresh(NSView *view, TRINodeLibraryRefresh refresh);
// Reconcile the current document and panels after a catalog generation changes.
BOOL TRIRefreshNodeCatalog(NSView *view, NSString **why);
BOOL TRIPrepareForNodeRefresh(NSView *view, NSString **why);
