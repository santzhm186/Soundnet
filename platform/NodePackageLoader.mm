#import "NodePackageLoader.h"
#include "NodePackage.hpp"
#include "NodeEditor.h"
#include "NodeSpec.hpp"
#include <CommonCrypto/CommonDigest.h>
#include <dlfcn.h>
#include <map>
#include <mutex>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
namespace tri {
namespace {
std::mutex loaderMutex;
struct Image {const NodePackageAPI* api=nullptr;std::shared_ptr<void> handle;std::string failure;};
// Objective-C classes cannot be unloaded safely. Retain a bounded number of
// generations for this process; replacement loads a unique immutable image.
std::map<std::string,Image>& images(){static std::map<std::string,Image> value;return value;}
std::string& activeFingerprint(){static std::string value;return value;}
// A process owns its immutable snapshots. A rejected candidate removes only
// directories created by that attempt; accepted generations stay readable.
struct SnapshotCache {
    NSString* root=[NSTemporaryDirectory() stringByAppendingPathComponent:
        [@"Soundnet-NodeImages-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    std::set<std::string> accepted;
    ~SnapshotCache(){@autoreleasepool {[[NSFileManager defaultManager]removeItemAtPath:root error:nil];}}
};
SnapshotCache& snapshots(){static SnapshotCache value;return value;}
struct SnapshotBatch {
    std::vector<std::string> created;
    bool committed=false;
    ~SnapshotBatch(){if(!committed)for(const auto& hash:created)
        [[NSFileManager defaultManager]removeItemAtPath:[snapshots().root stringByAppendingPathComponent:@(hash.c_str())] error:nil];}
    void commit(){for(const auto& hash:created)snapshots().accepted.insert(hash);committed=true;}
};
NSString* digest(NSData* data){
    unsigned char bytes[CC_SHA256_DIGEST_LENGTH];CC_SHA256(data.bytes,(CC_LONG)data.length,bytes);
    NSMutableString* text=[NSMutableString new];for(auto b:bytes)[text appendFormat:@"%02x",b];return text;
}
NSString* codeCacheRoot(){
    return [NSTemporaryDirectory() stringByAppendingPathComponent:@"Soundnet-NodeCode-ABI2"];
}
// Code signatures are cached by the OS against the immutable file identity.
// Reuse the same content-addressed inode across processes; resources remain in
// each process's private snapshots. Never rewrite an existing code image.
NSString* codeImage(NSData* data,const std::string& hash,std::string& why){
    auto* fm=NSFileManager.defaultManager;
    NSString* directory=[codeCacheRoot() stringByAppendingPathComponent:@(hash.c_str())];
    NSString* path=[directory stringByAppendingPathComponent:@"Node.dylib"];
    NSError* error=nil;
    if(![fm createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:&error]){
        why=error.localizedDescription.UTF8String ?: "无法准备节点代码缓存。";return nil;}
    auto validate=[&](){
        struct stat info{};
        if(lstat(path.fileSystemRepresentation,&info)!=0||!S_ISREG(info.st_mode))return false;
        NSData* stored=[NSData dataWithContentsOfFile:path options:NSDataReadingMappedIfSafe error:nil];
        return stored&&stored.length==data.length&&[digest(stored) isEqual:@(hash.c_str())];
    };
    struct stat existing{};
    if(lstat(path.fileSystemRepresentation,&existing)==0){
        if(!validate()){why="节点代码缓存损坏；请移除对应缓存目录后重试。";return nil;}
    }else if(errno!=ENOENT){why="无法检查节点代码缓存。";return nil;}
    else {
        NSString* staging=[directory stringByAppendingPathComponent:[@".staging-" stringByAppendingString:NSUUID.UUID.UUIDString]];
        if(![data writeToFile:staging options:NSDataWritingAtomic error:&error]||
           ![fm setAttributes:@{NSFilePosixPermissions:@0444} ofItemAtPath:staging error:&error]){
            [fm removeItemAtPath:staging error:nil];why=error.localizedDescription.UTF8String ?: "无法写入节点代码缓存。";return nil;}
        // link() publishes a complete, read-only inode exclusively. Another
        // process may win; its file must match before this process uses it.
        const int published=link(staging.fileSystemRepresentation,path.fileSystemRepresentation);
        const int linkError=errno;
        [fm removeItemAtPath:staging error:nil];
        if(published!=0&&(linkError!=EEXIST||!validate())){
            why=linkError==EEXIST?"节点代码缓存损坏；请移除对应缓存目录后重试。":"无法发布节点代码缓存。";return nil;}
    }
    // Touch the directory only: changing the dylib's timestamps invalidates
    // the OS signature cache and makes every subsequent launch cold again.
    [fm setAttributes:@{NSFileModificationDate:NSDate.date} ofItemAtPath:directory error:nil];
    return path;
}
void pruneCodeImages(){
    auto* fm=NSFileManager.defaultManager;
    NSArray<NSURL*>* folders=[fm contentsOfDirectoryAtURL:[NSURL fileURLWithPath:codeCacheRoot() isDirectory:YES]
        includingPropertiesForKeys:@[NSURLContentModificationDateKey] options:NSDirectoryEnumerationSkipsHiddenFiles error:nil];
    NSMutableArray<NSURL*>* complete=[NSMutableArray new];
    for(NSURL* folder in folders){
        NSString* name=folder.lastPathComponent;
        if(name.length!=64||[name rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"].invertedSet].location!=NSNotFound)continue;
        if([fm fileExistsAtPath:[[folder URLByAppendingPathComponent:@"Node.dylib"] path]])[complete addObject:folder];
    }
    if(complete.count<=512)return;
    NSArray<NSURL*>* oldest=[complete sortedArrayUsingComparator:^NSComparisonResult(NSURL* a,NSURL* b){
        NSDate *ad=nil,*bd=nil;[a getResourceValue:&ad forKey:NSURLContentModificationDateKey error:nil];
        [b getResourceValue:&bd forKey:NSURLContentModificationDateKey error:nil];return [(ad ?: NSDate.distantPast) compare:(bd ?: NSDate.distantPast)];
    }];
    NSUInteger remaining=complete.count;
    for(NSURL* folder in oldest){
        if(remaining<=512)break;
        // Keep all code used by this process (including rejected loaded ABIs).
        // Removing an older file does not invalidate another process's mapping.
        if(images().count(folder.lastPathComponent.UTF8String))continue;
        if([fm removeItemAtURL:folder error:nil])--remaining;
    }
}
struct CodeCacheMaintenance {~CodeCacheMaintenance(){pruneCodeImages();}};
bool nameValid(NSString* key){
    if(![key isKindOfClass:NSString.class]||!key.length||![key containsString:@"."])return false;
    NSCharacterSet* allowed=[NSCharacterSet characterSetWithCharactersInString:@"abcdefghijklmnopqrstuvwxyz0123456789._-"];
    return [key rangeOfCharacterFromSet:allowed.invertedSet].location==NSNotFound;
}
}
NodeRefreshResult refreshNodePackages(NSArray<NSURL*>* roots,bool requireEnvironment){
    std::lock_guard<std::mutex> guard(loaderMutex);NodeRefreshResult result;
    @autoreleasepool {
        NSMutableDictionary<NSString*,NSDictionary*>* selected=[NSMutableDictionary new];
        NSMutableArray<NSString*>* ordered=[NSMutableArray new];
        for(NSURL* root in roots){
            if(!root.isFileURL){result.message="节点目录必须位于本机。";return result;}
            NSError* error=nil;
            NSArray<NSURL*>* children=[[NSFileManager defaultManager]contentsOfDirectoryAtURL:root includingPropertiesForKeys:nil options:NSDirectoryEnumerationSkipsHiddenFiles error:&error];
            if(!children){if(error.code==NSFileReadNoSuchFileError||error.code==NSFileNoSuchFileError)continue;
                result.message=error.localizedDescription.UTF8String ?: "无法读取节点目录。";return result;}
            std::set<std::string> rootKeys;
            children=[children sortedArrayUsingComparator:^NSComparisonResult(NSURL* a,NSURL* b){return [a.lastPathComponent compare:b.lastPathComponent];}];
            for(NSURL* folder in children){
                if(![folder.pathExtension isEqual:@"soundnode"])continue;
                NSData* json=[NSData dataWithContentsOfURL:[folder URLByAppendingPathComponent:@"manifest.json"]];
                NSDictionary* manifest=json?[NSJSONSerialization JSONObjectWithData:json options:0 error:nil]:nil;
                if(![manifest isKindOfClass:NSDictionary.class]||!nameValid(manifest[@"key"])||
                   ![manifest[@"packageABI"] isEqual:@(nodePackageABI)]||![manifest[@"sdkVersion"] isEqual:@(nodeSDKVersion)]||
                   ![manifest[@"library"] isEqual:@"Node.dylib"]||![manifest[@"sha256"] isKindOfClass:NSString.class]){
                    result.message=std::string("节点包格式或 SDK 不受支持：")+(folder.lastPathComponent.UTF8String ?: "");return result;}
                NSString* key=manifest[@"key"];
                if(!rootKeys.insert(key.UTF8String).second){result.message=std::string("同一目录包含重复节点：")+key.UTF8String;return result;}
                if(!selected[key])[ordered addObject:key];
                selected[key]=@{@"manifest":manifest,@"url":folder};
            }
        }
        std::string fingerprint;SnapshotBatch candidate;CodeCacheMaintenance maintainCode;
        std::vector<NodeModule> modules;std::map<std::string,void*> editors;std::vector<std::shared_ptr<void>> owners;
        for(NSString* key in ordered){
            NSDictionary* row=selected[key];NSDictionary* manifest=row[@"manifest"];NSURL* folder=row[@"url"];
            NSURL* library=[folder URLByAppendingPathComponent:@"Node.dylib"];
            NSData* data=[NSData dataWithContentsOfURL:library options:NSDataReadingMappedIfSafe error:nil];
            if(!data||data.length>256*1024*1024||![digest(data) isEqual:manifest[@"sha256"]]){
                result.message=std::string("节点包文件不完整或校验失败：")+key.UTF8String;return result;}
            std::string hash=[manifest[@"sha256"] UTF8String];
            auto found=images().find(hash);
            if(found!=images().end()&&!found->second.failure.empty()){
                result.message=found->second.failure;return result;}
            NSDictionary* resources=manifest[@"resources"] ?: @{};
            if(![resources isKindOfClass:NSDictionary.class]){result.message="节点资源清单无效。";return result;}
            NSMutableString* identity=[NSMutableString stringWithFormat:@"%@:%@",key,manifest[@"sha256"]];
            NSMutableDictionary<NSString*,NSData*>* contents=[NSMutableDictionary new];
            for(NSString* relative in [[resources allKeys] sortedArrayUsingSelector:@selector(compare:)]){
                if(![relative isKindOfClass:NSString.class]||!relative.length||relative.isAbsolutePath||
                   [relative.pathComponents containsObject:@".."]||[relative.pathComponents containsObject:@"."]||
                   ![resources[relative] isKindOfClass:NSString.class]){result.message="节点资源路径无效。";return result;}
                NSURL* resourceRoot=[folder URLByAppendingPathComponent:@"Resources" isDirectory:YES];
                NSURL* source=[resourceRoot URLByAppendingPathComponent:relative];
                NSString* prefix=[resourceRoot.URLByResolvingSymlinksInPath.path stringByAppendingString:@"/"];
                if(![source.URLByResolvingSymlinksInPath.path hasPrefix:prefix]){result.message="节点资源超出包目录。";return result;}
                NSData* bytes=[NSData dataWithContentsOfURL:source options:NSDataReadingMappedIfSafe error:nil];
                if(!bytes||bytes.length>UINT32_MAX||![digest(bytes) isEqual:resources[relative]]){result.message="节点资源缺失或校验失败。";return result;}
                contents[relative]=bytes;[identity appendFormat:@"\n%@:%@",relative,resources[relative]];
            }
            NSString* packageHash=digest([identity dataUsingEncoding:NSUTF8StringEncoding]);
            fingerprint+=std::string(key.UTF8String)+":"+packageHash.UTF8String+"\n";
            NSString* directory=[snapshots().root stringByAppendingPathComponent:packageHash];
            NSString* resourceDirectory=[directory stringByAppendingPathComponent:@"Resources"];
            const std::string snapshotID=packageHash.UTF8String;
            if(!snapshots().accepted.count(snapshotID)){
                if(snapshots().accepted.size()+candidate.created.size()>=512){
                    result.message="本次运行已保存 512 个节点资源版本，请重启后继续刷新。";return result;}
                candidate.created.push_back(snapshotID);
                NSError* preparationError=nil;
                if(![[NSFileManager defaultManager]createDirectoryAtPath:resourceDirectory withIntermediateDirectories:YES
                        attributes:@{NSFilePosixPermissions:@0700} error:&preparationError]){
                    result.message=preparationError.localizedDescription.UTF8String ?: "无法准备节点资源。";return result;}
                for(NSString* relative in contents){
                    NSString* target=[resourceDirectory stringByAppendingPathComponent:relative];
                    if(![[NSFileManager defaultManager]createDirectoryAtPath:target.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:&preparationError]||
                       ![contents[relative] writeToFile:target options:NSDataWritingAtomic error:&preparationError]){
                        result.message=preparationError.localizedDescription.UTF8String ?: "无法准备节点资源。";return result;}
                }
            }
            if(found==images().end()){
                if(images().size()>=512){result.message="本次运行已加载 512 个节点版本，请重启后继续刷新。";return result;}
                NSString* path=codeImage(data,hash,result.message);
                if(!path)return result;
                void* handle=dlopen(path.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL);
                if(!handle){const char* why=dlerror();result.message=std::string("无法加载节点 ")+key.UTF8String+": "+(why?why:"");return result;}
                // Loading may register Objective-C classes even when its ABI is
                // rejected. Count and retain every opened image before entry().
                Image image;image.handle=std::shared_ptr<void>(handle,[](void*){});
                found=images().emplace(hash,std::move(image)).first;
                auto& loaded=found->second;
                try {@try {
                    auto entry=reinterpret_cast<NodePackageEntry>(dlsym(handle,"soundnet_node_v2"));
                    const NodePackageAPI* api=entry?entry():nullptr;
                    if(!api||api->size!=sizeof(NodePackageAPI)||api->abi!=nodePackageABI||api->sdk!=nodeSDKVersion||
                       api->descriptorSize!=sizeof(NodeSpec)||api->moduleSize!=sizeof(NodeModule)||api->configSize!=sizeof(KernelConfig)||api->contextSize!=sizeof(KernelContext)||
                       (api->editor&&api->editorContextSize!=sizeof(NodeEditorContext))||
                       !api->abiTag||std::strcmp(api->abiTag,nodeABITag)||!api->key||!api->module)
                        loaded.failure=std::string("节点二进制接口不匹配：")+key.UTF8String;
                    else loaded.api=api;
                }@catch(NSException* exception){loaded.failure=std::string("节点入口异常：")+(exception.reason.UTF8String ?: "Objective-C exception");}
                }catch(const std::exception& error){loaded.failure=std::string("节点入口异常：")+error.what();}
                 catch(...){loaded.failure="节点入口抛出了未知异常。";}
                if(!loaded.failure.empty()){result.message=loaded.failure;return result;}
            }
            // A bad manifest key must not poison a valid binary's cache entry.
            if(std::strcmp(found->second.api->key,key.UTF8String)){
                result.message=std::string("节点二进制身份不匹配：")+key.UTF8String;return result;}
            try{@try {auto module=found->second.api->module();if(!module.spec.key||std::strcmp(module.spec.key,key.UTF8String))throw std::logic_error("Node identity mismatch");
                module.resourceDirectory=resourceDirectory.UTF8String;
                modules.push_back(std::move(module));owners.push_back(found->second.handle);
                if(found->second.api->editor)editors.emplace(key.UTF8String,found->second.api->editor);
            }@catch(NSException* exception){result.message=exception.reason.UTF8String ?: "节点描述符异常。";return result;}
            }catch(const std::exception& error){result.message=error.what();return result;}
             catch(...){result.message="节点描述符抛出了未知异常。";return result;}
        }
        if(requireEnvironment){
            size_t clocks=0,settings=0;
            for(const auto& module:modules){const auto& spec=module.spec;
                if(spec.environmentRole==EnvironmentRole::None)continue;
                const char* expected=spec.environmentRole==EnvironmentRole::Timecode?"soundnet.timecode":"soundnet.project-settings";
                if(std::strcmp(spec.key,expected)||spec.nodeVersion!=1||spec.stateVersion!=1){result.message="环境节点不符合当前平台 SDK 合同，节点目录未更新。";return result;}
                if(spec.environmentRole==EnvironmentRole::Timecode)++clocks;else ++settings;
            }
            if(clocks!=1||settings!=1){result.message="节点目录必须保留当前平台的时间码和项目设定模块。";return result;}
        }
        if(fingerprint==activeFingerprint()){candidate.commit();result.ok=true;result.count=modules.size();result.message="节点目录没有变化。";return result;}
        try{auto catalog=makeNodeCatalog(std::move(modules),std::move(editors),std::move(owners));result.count=catalog->modules.size();
            {NodeCatalogReadScope scope(catalog);const auto problem=registryProblems();if(!problem.empty())throw std::logic_error(problem);}
            publishNodeCatalog(std::move(catalog));activeFingerprint()=std::move(fingerprint);candidate.commit();
        }catch(const std::exception& error){result.message=error.what();return result;}
        result.ok=true;result.changed=true;result.message="节点库已更新，共 "+std::to_string(result.count)+" 个可用节点。";return result;
    }
}
NSArray<NSURL*>* defaultNodePackageRoots(NSBundle* bundle){
    NSString* override=NSProcessInfo.processInfo.environment[@"SOUNDNET_NODE_PATH"];
    if(override.length){NSMutableArray* roots=[NSMutableArray new];for(NSString* path in [override componentsSeparatedByString:@":"])if(path.length)[roots addObject:[NSURL fileURLWithPath:path isDirectory:YES]];return roots;}
    NSURL* bundled=[bundle.resourceURL URLByAppendingPathComponent:@"Nodes" isDirectory:YES];
    NSURL* support=[[[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask] firstObject];
    NSURL* user=[[support URLByAppendingPathComponent:@"Soundnet" isDirectory:YES] URLByAppendingPathComponent:@"Nodes" isDirectory:YES];
    return user?@[bundled,user]:@[bundled];
}
NSBundle* nodePlatformBundle(){
    Dl_info info{};
    if(dladdr(reinterpret_cast<const void*>(&refreshNodePackages),&info)&&info.dli_fname){
        NSURL* library=[NSURL fileURLWithPath:@(info.dli_fname)];
        NSURL* bundle=[[[library URLByDeletingLastPathComponent] URLByDeletingLastPathComponent] URLByDeletingLastPathComponent];
        NSBundle* found=[NSBundle bundleWithURL:bundle];if(found)return found;
    }
    return NSBundle.mainBundle;
}
}
