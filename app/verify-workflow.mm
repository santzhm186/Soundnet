// Standalone preset regression harness. Uses a process-local preference store.
#import "TRIHost.h"
#include "PatchRouting.hpp"
#include <objc/runtime.h>
#include <cstdio>
#include <cstdlib>

@interface TRNTestDefaults:NSObject
@property(nonatomic,strong) NSMutableDictionary *saved;
@end
@implementation TRNTestDefaults
- (NSArray*)arrayForKey:(NSString*)key { return self.saved[key]; }
- (NSDictionary*)dictionaryForKey:(NSString*)key { return self.saved[key]; }
- (void)setObject:(id)value forKey:(NSString*)key { self.saved[key]=[value copy]; }
@end

static void require(bool ok,const char* what) {
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",what); std::exit(1); }
}
int main() {
    @autoreleasepool {
        TRNTestDefaults *prefs=[TRNTestDefaults new];
        prefs.saved=[NSMutableDictionary dictionary];
        auto original=class_getClassMethod(NSUserDefaults.class,@selector(standardUserDefaults));
        method_setImplementation(original,imp_implementationWithBlock(^id(id) { return prefs; }));
        for(const int count:{25,35}) {
            NSMutableArray* old=[NSMutableArray array];
            for(int i=0;i<count;++i) [old addObject:@(tri::params[i].def)];
            old[tri::Attack]=@.87f; old[tri::Pattern]=@2;
            if(count==35) old[tri::ArpRepeat]=@6;
            prefs.saved[@"Ternary.Patch.1"]=old;
            TRIHost* host=[TRIHost new];
            require([host value:tri::Attack]==.87f && [host value:tri::Pattern]==2,"pre-1.2 values restored");
            require([host value:tri::ArpRepeat]==(count==35?6:1),"repeats");
            for(int i=tri::RouteArpIn;i<=tri::RouteOutputIn;++i)
                require([host value:i]==tri::params[i].def,"default wiring for old version");
            require([host value:tri::layout::visibility]==1023 && [host value:tri::layout::x(tri::MixNode)]==-1,
                    "default visual layout");
        }
        TRIHost* editor=[TRIHost new];
        // Swapping EQ and delay would be cyclic if only the first edit took effect.
        [editor setValue:0 forParam:tri::layout::beginRouting];
        [editor setValue:static_cast<float>(tri::DelayNode) forParam:tri::RouteEQIn];
        [editor setValue:static_cast<float>(tri::MixNode) forParam:tri::RouteDelayIn];
        [editor setValue:static_cast<float>(tri::EQNode) forParam:tri::RouteOutputIn];
        require([editor value:tri::RouteOutputIn]==static_cast<float>(tri::DelayNode),"staged edits are private");
        [editor setValue:0 forParam:tri::layout::commitRouting];
        [editor setValue:351.5f forParam:tri::layout::x(tri::MixNode)];
        [editor setValue:87.f forParam:tri::layout::y(tri::MixNode)];
        [editor setValue:511 forParam:tri::layout::visibility];
        [editor setValue:tri::ClipNode forParam:tri::RouteModelIn];
        [editor setValue:tri::ClockNode forParam:tri::RouteClipClock];
        [editor setValue:1 forParam:tri::ClipLoop];
        NSMutableArray *pitches=[NSMutableArray array];
        for(int i=0;i<128;++i) [pitches addObject:@(127-i)];
        NSDictionary *assets=@{@"model":@{@"name":@"Invert",@"pitches":pitches}};
        require([editor replaceWorkflowAssets:assets],"accept pitch model");
        require(![editor replaceWorkflowAssets:@{@"clip":@{}}],"reject invalid asset before mutation");
        [editor savePatch];
        TRIHost* recalled=[TRIHost new];
        require([recalled value:tri::RouteEQIn]==static_cast<float>(tri::DelayNode) &&
                [recalled value:tri::RouteDelayIn]==static_cast<float>(tri::MixNode) &&
                [recalled value:tri::RouteOutputIn]==static_cast<float>(tri::EQNode),"swapped FX topology recalled");
        require([recalled value:tri::RouteModelIn]==tri::ClipNode &&
                [recalled value:tri::RouteClipClock]==tri::ClockNode && [recalled value:tri::ClipLoop]==1,
                "extended noncontiguous route parameters recalled");
        require([recalled.workflowAssets isEqual:assets],"validated model survives save and reopen");
        require([recalled value:tri::layout::x(tri::MixNode)]==351.5f &&
                [recalled value:tri::layout::y(tri::MixNode)]==87.f &&
                [recalled value:tri::layout::visibility]==511,"visual layout recalled");
        puts("PASS — standalone 1.0/1.1 migration, atomic FX rewire, route/layout/visibility recall, 1.4 clip/model ports and model persistence.");
    }
}
