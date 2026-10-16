#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
namespace sideshow {
class IsfBudget {
 struct State;
public:
 class Lease {
 public:
  Lease()=default;~Lease();Lease(Lease&&) noexcept;Lease& operator=(Lease&&) noexcept;
  Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
 private:
  friend class IsfBudget;std::shared_ptr<State> state_;uint64_t owner_=0;size_t bytes_=0;void release();
 };
 explicit IsfBudget(size_t totalLimit=256u*1024*1024);
 uint64_t newOwner();Lease reserve(uint64_t owner,size_t bytes);size_t used()const;
private:std::shared_ptr<State> state_;
};
size_t isfTextureBytes(int width,int height,size_t bytesPerPixel);
}
