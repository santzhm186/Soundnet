#pragma once
#include "NodeDescriptor.hpp"
#include <cstddef>
#define SOUNDNET_STRINGIFY_INNER(x) #x
#define SOUNDNET_STRINGIFY(x) SOUNDNET_STRINGIFY_INNER(x)
namespace tri {
inline constexpr unsigned nodePackageABI=2;
inline constexpr const char* nodeABITag="Soundnet-SDK2-macOS-libc++-clang" SOUNDNET_STRINGIFY(__clang_major__);
// Only this symbol is located by name. Its version/layout are checked before
// invoking a module. C++ and Cocoa use the platform SDK's pinned ABI/toolchain.
struct NodePackageAPI {
    uint32_t size=sizeof(NodePackageAPI);
    uint32_t abi=nodePackageABI;
    uint32_t sdk=nodeSDKVersion;
    uint32_t descriptorSize=sizeof(NodeSpec);
    uint32_t moduleSize=sizeof(NodeModule);
    uint32_t configSize=sizeof(KernelConfig);
    uint32_t contextSize=sizeof(KernelContext);
    uint32_t editorContextSize=0;
    const char* abiTag=nodeABITag;
    const char* key=nullptr;
    NodeModule (*module)()=nullptr;
    void* editor=nullptr;
};
using NodePackageEntry=const NodePackageAPI*(*)();
}
