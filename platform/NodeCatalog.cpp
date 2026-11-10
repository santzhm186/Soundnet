#include "NodeCatalog.hpp"
namespace tri {
namespace {
std::shared_ptr<const NodeCatalog>& storage() {
    static std::shared_ptr<const NodeCatalog> value=makeNodeCatalog({});return value;
}
}
std::shared_ptr<const NodeCatalog>& nodeCatalogReadOverride(){static thread_local std::shared_ptr<const NodeCatalog> value;return value;}
std::shared_ptr<const NodeCatalog> nodeCatalogSnapshot(){
    if(nodeCatalogReadOverride())return nodeCatalogReadOverride();
    return std::atomic_load(&storage());
}
void publishNodeCatalog(std::shared_ptr<NodeCatalog> catalog) {
    static std::mutex mutex;
    static std::vector<std::shared_ptr<const NodeCatalog>> retired;
    std::lock_guard<std::mutex> lock(mutex);
    auto previous=std::atomic_load(&storage());
    if(retired.size()>=512)throw std::runtime_error("Restart required after 512 catalog revisions");
    retired.push_back(previous);catalog->generation=previous->generation+1;
    std::atomic_store(&storage(),std::shared_ptr<const NodeCatalog>(std::move(catalog)));
}
}
