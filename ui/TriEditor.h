#import <Cocoa/Cocoa.h>
typedef float (^TRIRead)(int param);
typedef void (^TRIWrite)(int param, float value);
typedef void (^TRIMIDI)(unsigned char status, unsigned char d1, unsigned char d2);
typedef BOOL (^TRIInputRead)(void);
typedef void (^TRIInputWrite)(BOOL enabled);
typedef NSString * (^TRIStatus)(void);
typedef NSDictionary * (^TRIAssetsRead)(void);
typedef BOOL (^TRIAssetsWrite)(NSDictionary *assets);
// Fixed 1144 x 650 points. Pass nil for input switches in AU (host routes audio).
// status is a short connection/engine status, e.g. @"CoreMIDI · 2 sources".
NSView *TRICreateEditor(TRIRead read, TRIWrite write, TRIMIDI midi, TRIInputRead inputRead, TRIInputWrite inputWrite, TRIStatus status,
                        TRIAssetsRead assetsRead = nullptr, TRIAssetsWrite assetsWrite = nullptr);
