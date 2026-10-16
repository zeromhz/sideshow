#include "isf_loader.h"
#include "isf_files.h"
#include "isf_document.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace sideshow {
struct IsfLoader::Impl {
 struct Garbage {
  std::mutex mutex;std::vector<const IsfSource*> pending;std::atomic<bool> waiting{false};
  void drain(){std::vector<const IsfSource*> local;{std::lock_guard<std::mutex> l(mutex);local.swap(pending);waiting=false;}for(auto* p:local)delete p;}
  ~Garbage(){drain();}
 };
 std::filesystem::path root;std::mutex mutex;std::condition_variable wake;bool stop=false;uint64_t serial=0,session=0,generation=0;bool catalogPending=false;
 struct Request{uint64_t session,generation;std::string id;};std::optional<Request> request;
 std::optional<IsfLoadEvent> result;std::shared_ptr<const IsfCatalog> catalog;std::shared_ptr<Garbage> garbage=std::make_shared<Garbage>();std::thread worker;
 explicit Impl(std::filesystem::path p):root(std::move(p)),worker([this]{run();}){}
 ~Impl(){ {std::lock_guard<std::mutex> l(mutex);stop=true;}wake.notify_one();worker.join();result.reset();garbage->drain();}
 bool cancelled(const Request& r){std::lock_guard<std::mutex> l(mutex);return stop||session!=r.session||generation!=r.generation;}
 void run(){
  auto initial=std::make_shared<IsfCatalog>(IsfCatalog::scan(root,[this]{std::lock_guard<std::mutex> l(mutex);return stop;}));
  {std::lock_guard<std::mutex> l(mutex);catalog=std::move(initial);}wake.notify_all();
  while(true){
   garbage->drain();std::optional<Request> work;
   {std::unique_lock<std::mutex> l(mutex);wake.wait_for(l,std::chrono::milliseconds(25),[&]{return stop||request.has_value()||garbage->waiting.load();});if(stop)break;work=std::move(request);request.reset();}
   if(!work)continue;auto r=std::move(*work);auto cancel=[&]{return cancelled(r);};IsfLoadEvent event{IsfLoadKind::Source,r.session,r.generation,{},{},{}};
   try{
    if(cancel())continue;IsfFiles files(root);auto fragment=files.source(r.id,cancel);auto vertex=files.vertexFor(r.id,cancel);
    auto owned=std::make_unique<IsfSource>();owned->document=parseIsfDocument(r.id,std::move(fragment),vertex?std::move(*vertex):"");size_t bytes=0;
    for(const auto& import:owned->document.imports){if(cancel())break;auto encoded=files.imported(r.id,import.path,32u*1024*1024,cancel);if(isfImageBytes(encoded)>128u*1024*1024-bytes)throw std::runtime_error("ISF imported images exceed 128 MiB");auto image=decodeIsfImage(import.name,encoded);if(image.rgba.size()>128u*1024*1024-bytes)throw std::runtime_error("ISF imported images exceed 128 MiB");bytes+=image.rgba.size();owned->images.push_back(std::move(image));}
    if(cancel())continue;auto g=garbage;event.source=std::shared_ptr<const IsfSource>(owned.release(),[g](const IsfSource* p){std::lock_guard<std::mutex> l(g->mutex);g->pending.push_back(p);g->waiting=true;});
   }catch(const std::exception& e){event.kind=IsfLoadKind::Failed;event.error=e.what();}
   std::optional<IsfLoadEvent> old;
   {std::lock_guard<std::mutex> l(mutex);if(!stop&&session==r.session&&generation==r.generation){old=std::move(result);result=std::move(event);}}
  }
  garbage->drain();
 }
};
IsfLoader::IsfLoader(std::filesystem::path root):impl_(std::make_unique<Impl>(std::move(root))){}
IsfLoader::~IsfLoader()=default;
uint64_t IsfLoader::acquireSession(){std::lock_guard<std::mutex> l(impl_->mutex);impl_->session=++impl_->serial;impl_->generation=0;impl_->catalogPending=true;impl_->request.reset();impl_->result.reset();return impl_->session;}
void IsfLoader::request(uint64_t s,uint64_t g,std::string id){std::lock_guard<std::mutex> l(impl_->mutex);if(s!=impl_->session||!s||g<impl_->generation)return;impl_->generation=g;impl_->result.reset();impl_->request=Impl::Request{s,g,std::move(id)};impl_->wake.notify_one();}
void IsfLoader::cancel(uint64_t s){std::lock_guard<std::mutex> l(impl_->mutex);if(s!=impl_->session)return;impl_->session=0;impl_->request.reset();impl_->result.reset();impl_->catalogPending=false;impl_->wake.notify_one();}
std::optional<IsfLoadEvent> IsfLoader::poll(uint64_t s){std::lock_guard<std::mutex> l(impl_->mutex);if(!s||s!=impl_->session)return std::nullopt;if(impl_->catalogPending){if(!impl_->catalog)return std::nullopt;impl_->catalogPending=false;return IsfLoadEvent{IsfLoadKind::Catalog,s,0,impl_->catalog,{},{}};}auto result=std::move(impl_->result);impl_->result.reset();return result;}
}
