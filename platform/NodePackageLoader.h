#pragma once
#import <Foundation/Foundation.h>
#include "NodeCatalog.hpp"
namespace tri {
struct NodeRefreshResult {
    bool ok=false,changed=false;
    size_t count=0;
    std::string message;
};
// Search roots are ordered: later roots override a bundled key. Within a root
// duplicate keys are rejected. Publication is all-or-nothing on the control
// thread. Live kernels retain their original catalog and code until retired.
NodeRefreshResult refreshNodePackages(NSArray<NSURL*>* roots,bool requireEnvironment=false);
NSArray<NSURL*>* defaultNodePackageRoots(NSBundle* platformBundle);
NSBundle* nodePlatformBundle();
}
