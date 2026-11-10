// This runner knows no fixture implementation and contains no generated registry.
// The same executable/platform library loads all revisions in one process.
#import <Cocoa/Cocoa.h>
#include "../platform/NodePackageLoader.h"
#include "../platform/NodeEditor.h"
#include "../platform/runtime/InstanceGraph.hpp"
#include "../src/Engine.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <sys/stat.h>
#ifndef SOUNDNET_DYNAMIC_NODES
#error NodePackageTests must link the shared platform with dynamic nodes enabled
#endif
@interface NSView (SoundnetPackageTestMarker)
-(NSInteger)revisionMarker;
@end
namespace {
void require(bool condition,const char* message) {
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
NSURL *root(NSString *base,NSString *name) {
    return [NSURL fileURLWithPath:[base stringByAppendingPathComponent:name] isDirectory:YES];
}
tri::NodeRefreshResult refresh(NSArray<NSURL*> *roots,bool changed) {
    auto result=tri::refreshNodePackages(roots);
    if(!result.ok)std::fprintf(stderr,"Refresh error: %s\n",result.message.c_str());
    require(result.ok,"candidate packages load");require(result.changed==changed,"refresh changed flag");return result;
}
float process(tri::NodeKernel& kernel) {
    float left[7]{},right[7]{};
    tri::KernelOutput output;output.id="audio";output.left=left;output.right=right;
    tri::KernelContext context;context.frames=7;context.outputs=&output;context.outputCount=1;
    kernel.process(context);
    for(int i=0;i<7;++i)require(left[i]==left[0]&&right[i]==left[0],"fixture produces deterministic stereo block");
    return left[0];
}
float render(tri::InstanceGraph& graph) {
    float left[19]{},right[19]{};graph.render(nullptr,nullptr,left,right,19);
    for(int i=0;i<19;++i)require(left[i]==left[0]&&right[i]==left[0],"published graph produces stereo output");
    require(graph.kernelFaultCount()==0,"valid module emits no kernel contract faults");
    return left[0];
}
tri::GraphSpec fixtureGraph() {
    const auto* spec=tri::nodeSpecForKey("test.refresh-fixture");require(spec,"fixture descriptor discovered dynamically");
    tri::GraphNode node;node.id=1;node.key=spec->key;node.type=spec->type;
    node.nodeVersion=spec->nodeVersion;node.stateVersion=spec->stateVersion;
    tri::GraphSpec graph;graph.projectId="package-test";graph.nodes.push_back(node);return graph;
}
float resourceValue(const tri::KernelConfig& config) {
    require(!config.resourceDirectory.empty(),"platform supplies immutable package resource root");
    std::ifstream input(config.resourceDirectory+"/value.txt");float result=-999;input>>result;
    require(bool(input),"package resource snapshot remains readable");return result;
}
NSView* editor(const tri::KernelConfig& config) {
    auto factory=reinterpret_cast<tri::NodeEditorFactory>(tri::loadedNodeEditor("test.refresh-fixture"));
    require(factory,"node-owned UI factory dynamically registered");
    tri::NodeEditorContext context;context.resourceDirectory=@(config.resourceDirectory.c_str());
    NSView* view=factory(context);require(view!=nil,"node-owned UI factory executes");return view;
}
void rejects(NSURL *directory,std::shared_ptr<const tri::NodeCatalog> accepted) {
    tri::NodeRefreshResult failed;bool escaped=false;
    try {failed=tri::refreshNodePackages(@[directory]);}catch(...){escaped=true;}
    require(!escaped,"invalid module entry cannot throw through package refresh");
    require(!failed.ok&&!failed.changed&&!failed.message.empty(),"invalid batch is explicitly rejected");
    require(tri::nodeCatalogSnapshot()==accepted,"rejected batch never publishes a partial catalog");
    require(!tri::nodeSpecForKey("test.additional"),"valid newcomer from rejected batch is not registered");
}
}
int main(int argc,const char* argv[]) { @autoreleasepool {
    require(argc==2,"usage: NodePackageTests fixture-root");NSString* base=@(argv[1]);
    [NSApplication sharedApplication];
    NSString* entryLog=[base stringByAppendingPathComponent:@"entry-calls.log"];
    [[NSFileManager defaultManager]removeItemAtPath:entryLog error:nil];
    setenv("SOUNDNET_PACKAGE_ENTRY_LOG",entryLog.fileSystemRepresentation,1);
    require(tri::nodeCatalogSnapshot()->modules.empty(),"fixed platform starts without statically linked nodes");
    {
        auto host=std::make_unique<tri::Engine>();host->prepare(48000);
        host->midi(0x90,60,100);host->dawMidi(0x90,67,100);
        float left[128],right[128],input[128];std::fill_n(input,128,.5f);
        for(int block=0;block<8;++block){
            host->render(input,input,left,right,128);
            for(int i=0;i<128;++i)require(left[i]==0&&right[i]==0,
                "host without an instance graph cannot play embedded legacy nodes");
        }
    }
    refresh(@[root(base,@"empty")],false);
    auto first=refresh(@[root(base,@"v1")],true);require(first.count==1,"initial discovery adds one node");
    auto oldCatalog=tri::nodeCatalogSnapshot();
    auto graphSpec=fixtureGraph();auto oldConfig=tri::resolvedKernelConfig(graphSpec.nodes[0]);
    const auto oldFactory=tri::nodeSpecForKey("test.refresh-fixture")->kernelFactory;
    auto oldKernel=oldFactory();oldKernel->prepare(48000,128);oldKernel->configure(oldConfig);
    require(process(*oldKernel)==.25f&&resourceValue(oldConfig)==1,"first independently compiled algorithm/default");
    __attribute__((objc_precise_lifetime)) NSView* oldView=editor(oldConfig);
    require([oldView revisionMarker]==1,"first UI implementation");
    tri::InstanceGraph live;live.prepare(48000);require(live.publish(graphSpec),"first graph publishes");
    require(render(live)==.25f,"first binary runs through the actual graph");
    NSString* firstResource=[@(oldConfig.resourceDirectory.c_str()) stringByAppendingPathComponent:@"value.txt"];
    NSNumber* originalInode=[[NSFileManager defaultManager]attributesOfItemAtPath:firstResource error:nil][NSFileSystemFileNumber];
    refresh(@[root(base,@"v1")],false);require(tri::nodeCatalogSnapshot()==oldCatalog,"unchanged directory preserves generation");
    require([originalInode isEqual:[[NSFileManager defaultManager]attributesOfItemAtPath:firstResource error:nil][NSFileSystemFileNumber]],
            "unchanged refresh never rewrites a published resource snapshot");
    NSString* snapshotRoot=[[@(oldConfig.resourceDirectory.c_str()) stringByDeletingLastPathComponent] stringByDeletingLastPathComponent];
    NSUInteger snapshotCount=[[[NSFileManager defaultManager]contentsOfDirectoryAtPath:snapshotRoot error:nil] count];

    // Production hosts require both SDK environment modules. Partial fixture
    // catalogs remain useful with the default false policy, but strict refresh
    // must reject empty, unchanged and changed partial directories atomically.
    for(NSArray<NSURL*>* candidate in @[@[root(base,@"empty")],@[root(base,@"v1")],
                                        @[root(base,@"v2"),root(base,@"additional")]]) {
        const auto rejected=tri::refreshNodePackages(candidate,true);
        require(!rejected.ok&&!rejected.changed&&!rejected.message.empty(),
                "production refresh rejects a catalog without required environment modules");
        require(tri::nodeCatalogSnapshot()==oldCatalog,
                "missing environment modules cannot replace the accepted catalog");
        require(!tri::nodeSpecForKey("test.additional")&&
                tri::nodeSpecForKey("test.refresh-fixture")->kernelFactory==oldFactory,
                "strict rejection neither adds modules nor replaces existing factories");
        require(render(live)==.25f,"missing environment modules leave the current graph running");
    }

    // Independently invalid candidate batches, each also containing a
    // valid new node, cannot partially change the accepted platform catalog.
    for(NSString* invalid in @[@"duplicate",@"bad-sdk",@"bad-sha",@"bad-resource"])
        rejects(root(base,invalid),oldCatalog);
    for(NSString* invalid in @[@"bad-abi",@"entry-throws"]) {
        std::string firstFailure;
        for(int repeat=0;repeat<4;++repeat) {
            rejects(root(base,invalid),oldCatalog);
            const auto failure=tri::refreshNodePackages(@[root(base,invalid)]);
            if(repeat==0)firstFailure=failure.message;
            require(!failure.ok&&failure.message==firstFailure,"rejected entry keeps one stable cached diagnostic");
        }
        NSString* calls=[NSString stringWithContentsOfFile:entryLog encoding:NSUTF8StringEncoding error:nil];
        require([[calls componentsSeparatedByString:invalid] count]==2,"a rejected binary entry executes only once per process");
    }
    require([[[NSFileManager defaultManager]contentsOfDirectoryAtPath:snapshotRoot error:nil] count]==snapshotCount,
            "rejected catalogs and binary entries remove all unpublished snapshots");
    require(render(live)==.25f,"failed refresh leaves existing graph running");

    // Later roots override the bundled key. The contract version stays 1;
    // only this node's package revision/default/algorithm changes.
    refresh(@[root(base,@"v1"),root(base,@"v2")],true);
    auto secondCatalog=tri::nodeCatalogSnapshot();
    require(secondCatalog!=oldCatalog&&secondCatalog->generation>oldCatalog->generation,"replacement publishes a new immutable generation");
    const auto* secondSpec=tri::nodeSpecForKey("test.refresh-fixture");
    require(secondSpec->nodeVersion==1&&secondSpec->stateVersion==1,"independent package update preserves behavior/state contract");
    require(secondSpec->kernelFactory!=oldFactory,"replacement comes from a different loaded code image");
    auto secondConfig=tri::resolvedKernelConfig(graphSpec.nodes[0]);
    auto secondKernel=secondSpec->kernelFactory();secondKernel->prepare(48000,128);secondKernel->configure(secondConfig);
    require(process(*secondKernel)==1&&process(*oldKernel)==.25f,"old and new kernels execute their own code");
    __attribute__((objc_precise_lifetime)) NSView* secondView=editor(secondConfig);
    require([secondView revisionMarker]==2&&[oldView revisionMarker]==1,"both UI revisions retain their own implementation");
    require(oldView.class!=secondView.class,"builder isolates Objective-C class names across revisions");
    require(render(live)==.25f,"old plan remains valid until its host publishes a replacement");
    require(live.publish(graphSpec)&&render(live)==1,"rebuilding the same graph adopts the new binary/default");

    // A valid parameter can reach a throwing third-party validator or
    // configure function. Neither exception may escape or commit a new
    // project identity before the whole candidate graph is ready.
    const int gainID=tri::nodeParamIdForKey(secondSpec->type,"gain");
    require(gainID>=0,"fixture gain is a declared public parameter");
    for(float throwingValue:{3.f,4.f}) {
        auto rejectedGraph=graphSpec;
        rejectedGraph.projectId=throwingValue==3?"rejected-validator-project":"rejected-configure-project";
        rejectedGraph.nodes[0].parameters[gainID]=throwingValue;
        const auto before=live.timecodeSnapshot();
        tri::CompiledGraph compiled;std::string why;
        bool compilerResult=false,published=false,escaped=false;
        try {compilerResult=tri::compileGraph(rejectedGraph,compiled,&why);}catch(...){escaped=true;}
        require(!escaped,"third-party validation exceptions cannot escape the graph compiler");
        if(throwingValue==3)
            require(!compilerResult&&why.find("fixture validator exception")!=std::string::npos,
                    "throwing validator rejects the candidate with its diagnostic");
        else require(compilerResult,"configure failure occurs only after successful graph validation");
        try {published=live.publish(rejectedGraph);}catch(...){escaped=true;}
        require(!escaped&&!published,"throwing module cannot publish or throw through the host");
        require(render(live)==1,"failed candidate keeps previous graph output");
        auto after=live.timecodeSnapshot();
        require(after.generation==before.generation&&after.framePosition==before.framePosition+19,
                "failed project leaves the playing timeline and generation intact");
        require(live.publish(graphSpec)&&render(live)==1,"normal publication still succeeds after a module exception");
        const auto restored=live.timecodeSnapshot();
        require(restored.generation==after.generation&&restored.framePosition==after.framePosition+19,
                "failed project ID did not mutate the control-side project identity");
    }

    auto added=refresh(@[root(base,@"v2"),root(base,@"additional")],true);
    require(added.count==2&&tri::nodeSpecForKey("test.additional"),"new independently built key is discovered");
    refresh(@[root(base,@"v2")],true);require(!tri::nodeSpecForKey("test.additional"),"removing a package removes its registration");

    // This directory has exactly the v2 Node.dylib bytes, only its resource
    // and manifest resource digest differ. Old snapshots must stay immutable.
    refresh(@[root(base,@"resource-only")],true);
    auto resourceConfig=tri::resolvedKernelConfig(graphSpec.nodes[0]);
    require(resourceConfig.resourceDirectory!=secondConfig.resourceDirectory,"resource update has a distinct immutable snapshot");
    require(resourceValue(resourceConfig)==.5f&&resourceValue(secondConfig)==1&&resourceValue(oldConfig)==1,"resource-only update preserves old resource contents");
    require(live.publish(graphSpec)&&render(live)==.5f,"new graph observes resource-only update without recompiling code");
    require(process(*secondKernel)==1&&process(*oldKernel)==.25f,"old instances keep prepared resources and code");

    refresh(@[root(base,@"empty")],true);
    require(!tri::nodeSpecForKey("test.refresh-fixture")&&tri::nodeCatalogSnapshot()->modules.empty(),"all packages can be removed from the current catalog");
    require(render(live)==.5f&&process(*oldKernel)==.25f,"removed module remains safe for plans and instances that own its code");
    require(!live.publish(graphSpec),"strict graph publication refuses a missing node");
    auto placeholder=graphSpec;
    placeholder.nodes[0].type=0;placeholder.nodes[0].unavailable=true;
    placeholder.nodes[0].unavailableReason="Missing test.refresh-fixture"; // shape produced by the document decoder
    require(live.publish(placeholder,true)&&render(live)==0,"editor graph can publish a silent missing-node placeholder");
    require([oldView revisionMarker]==1&&[secondView revisionMarker]==2,"retained old UI code remains valid after removal");
    // A fresh process must reuse exactly the code inode and timestamps; only
    // private resource snapshots are recreated. The script invokes us twice.
    NSMutableDictionary* identities=[NSMutableDictionary new];
    for(NSString* variant in @[@"v1",@"v2",@"additional",@"bad-abi",@"entry-throws"]) {
        NSString* key=[variant isEqual:@"additional"]?@"test.additional":
            ([variant isEqual:@"bad-abi"]?@"test.bad-abi":([variant isEqual:@"entry-throws"]?@"test.entry-throws":@"test.refresh-fixture"));
        NSString* manifestPath=[[[base stringByAppendingPathComponent:variant] stringByAppendingPathComponent:[key stringByAppendingString:@".soundnode"]] stringByAppendingPathComponent:@"manifest.json"];
        NSDictionary* manifest=[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:manifestPath] options:0 error:nil];
        NSString* hash=manifest[@"sha256"];
        NSString* code=[[[NSTemporaryDirectory() stringByAppendingPathComponent:@"Soundnet-NodeCode-ABI2"] stringByAppendingPathComponent:hash] stringByAppendingPathComponent:@"Node.dylib"];
        struct stat info{};require(stat(code.fileSystemRepresentation,&info)==0,"code cache survives resource snapshot lifecycle");
        identities[hash]=@[@(info.st_ino),@(info.st_mtimespec.tv_sec),@(info.st_mtimespec.tv_nsec),@(info.st_ctimespec.tv_sec),@(info.st_ctimespec.tv_nsec)];
        require((info.st_mode&0222)==0,"cached code image is read-only");
    }
    NSString* identityFile=[base stringByAppendingPathComponent:@"code-cache-inodes.json"];
    NSData* previousBytes=[NSData dataWithContentsOfFile:identityFile];
    if(previousBytes){
        NSDictionary* previous=[NSJSONSerialization JSONObjectWithData:previousBytes options:0 error:nil];
        require([previous isEqual:identities],"a second process preserves code inode, mtime and ctime");
        std::puts("PASS: second process reuses immutable code inodes and timestamps");
    }else require([[NSJSONSerialization dataWithJSONObject:identities options:0 error:nil] writeToFile:identityFile atomically:YES],"write first-process code identities");
    std::puts("PASS: fixed-platform node refresh, atomic rejection, exception isolation, resources, UI generations and live graph rebuild");
} }
