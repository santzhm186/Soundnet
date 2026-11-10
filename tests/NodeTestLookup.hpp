#pragma once
#include "../platform/GraphSpec.hpp"
#include <cassert>
#include <string>
namespace ntest {
inline int type(const char* slug) {
    const std::string key=std::string("soundnet.")+slug;
    const int result=tri::nodeTypeForKey(key.c_str());assert(result!=0);return result;
}
inline int param(const char* slug,const char* key) {
    const int result=tri::nodeParamIdForKey(type(slug),key);assert(result>=0);return result;
}
inline int param(const tri::GraphNode& node,const char* key) {
    const int type=node.key.empty()?node.type:tri::nodeTypeForKey(node.key.c_str());
    const int result=tri::nodeParamIdForKey(type,key);assert(result>=0);return result;
}
}
