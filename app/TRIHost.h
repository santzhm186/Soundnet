#import <Cocoa/Cocoa.h>

@interface TRIHost : NSObject
- (void)start;
- (void)stop;
- (void)restartAudio;
- (float)value:(int)param;
- (void)setValue:(float)value forParam:(int)param;
- (void)sendStatus:(uint8_t)status data1:(uint8_t)a data2:(uint8_t)b;
- (void)panic;
- (void)savePatch;
@property(nonatomic, readonly) NSString *status;
@property(nonatomic, getter=isInputEnabled) BOOL inputEnabled;
+ (int)runOfflineCheck;
@end
