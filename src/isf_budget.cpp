#include "isf_budget.h"
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>
namespace sideshow {
struct IsfBudget::State {std::mutex mutex;size_t limit,used=0;uint64_t next=0;std::map<uint64_t,size_t> owners;explicit State(size_t n):limit(n){}};
IsfBudget::IsfBudget(size_t n):state_(std::make_shared<State>(n)){}
uint64_t IsfBudget::newOwner(){std::lock_guard<std::mutex> l(state_->mutex);return ++state_->next;}
IsfBudget::Lease IsfBudget::reserve(uint64_t owner,size_t bytes){std::lock_guard<std::mutex> l(state_->mutex);auto i=state_->owners.find(owner);size_t old=i==state_->owners.end()?0:i->second;
 if(!owner||owner>state_->next||bytes>128u*1024*1024-old||bytes>state_->limit-state_->used)throw std::runtime_error("ISF texture budget exceeded");
 if(!bytes)return {};
 state_->owners[owner]=old+bytes;state_->used+=bytes;Lease lease;lease.state_=state_;lease.owner_=owner;lease.bytes_=bytes;return lease;
}
size_t IsfBudget::used()const{std::lock_guard<std::mutex> l(state_->mutex);return state_->used;}
void IsfBudget::Lease::release(){if(!state_)return;{std::lock_guard<std::mutex> l(state_->mutex);auto i=state_->owners.find(owner_);i->second-=bytes_;state_->used-=bytes_;if(!i->second)state_->owners.erase(i);}state_.reset();}
IsfBudget::Lease::~Lease(){release();}
IsfBudget::Lease::Lease(Lease&& other)noexcept:state_(std::move(other.state_)),owner_(other.owner_),bytes_(other.bytes_){}
IsfBudget::Lease& IsfBudget::Lease::operator=(Lease&& other)noexcept{if(this!=&other){release();state_=std::move(other.state_);owner_=other.owner_;bytes_=other.bytes_;}return *this;}
size_t isfTextureBytes(int w,int h,size_t bpp){if(w<=0||h<=0||w>4096||h>4096||!bpp||bpp>SIZE_MAX/size_t(w)/size_t(h))throw std::runtime_error("Invalid ISF texture dimensions");return size_t(w)*size_t(h)*bpp;}
}
