#pragma once
#include <memory>
namespace sideshow {
class IsfContext {
public:
    IsfContext();
    ~IsfContext();
    IsfContext(const IsfContext&)=delete;
    IsfContext& operator=(const IsfContext&)=delete;
    struct Impl;
    Impl& impl();
private:
    std::unique_ptr<Impl> impl_;
};
}
