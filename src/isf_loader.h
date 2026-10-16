#pragma once
#include "isf_catalog.h"
#include "isf_images.h"
#include <memory>
namespace sideshow {
enum class IsfLoadKind {Catalog,Source,Failed,Released};
struct IsfLoadEvent {IsfLoadKind kind;uint64_t session=0,generation=0;std::shared_ptr<const IsfCatalog> catalog;std::shared_ptr<const IsfSource> source;std::string error;};
class IsfLoader {
public:
 explicit IsfLoader(std::filesystem::path root);~IsfLoader();
 uint64_t acquireSession();void request(uint64_t,uint64_t,std::string);void cancel(uint64_t);std::optional<IsfLoadEvent> poll(uint64_t);
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
