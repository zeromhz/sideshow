#include "isf_backend.h"
#include "isf_document.h"
#include "log.h"
#include <algorithm>
#include <cmath>
#include <random>
namespace sideshow {
struct IsfBackend::Impl {
 IsfLoader& loader;IsfContext& context;IsfBudget& budget;Config::Isf config;PlaybackSettings settings;int width,height;
 uint64_t session,generation=0,frame=0;std::shared_ptr<const IsfCatalog> catalog;std::unique_ptr<IsfRuntime> runtime;std::unique_ptr<IsfParameters> parameters,lastGoodParameters;IsfAudio audio;
 VisualSelection selected;std::string requested,requestedFolder,error;bool loading=true,prepared=false,dirty=false,autoStopped=false,startup=true,folderRequest=false,controlsAllowed=true;
 double time=0,elapsed=0;std::vector<std::string> candidates;size_t candidateIndex=0;std::vector<std::variant<cmd::IsfParam,cmd::IsfEvent>> controls;std::mt19937 random{std::random_device{}()};
 Impl(const BackendInit& init,IsfLoader& l,IsfContext& c,IsfBudget& b):loader(l),context(c),budget(b),config(init.config.isf),settings(init.playback),width(init.width),height(init.height),session(loader.acquireSession()),selected(init.selection),requested(init.selection.preset),requestedFolder(init.selection.playlist){++generation;}
 ~Impl(){loader.cancel(session);}
 std::pair<int,int> dimensions()const{int maxW=config.maxWidth,maxH=config.maxHeight;if(height>width)std::swap(maxW,maxH);double scale=std::min({1.0,double(maxW)/std::max(1,width),double(maxH)/std::max(1,height)});return {std::max(1,int(width*scale)),std::max(1,int(height*scale))};}
 void fail(const std::string& why){error=why;logWarn("ISF: "+why);if(candidateIndex+1<candidates.size()){requested=candidates[++candidateIndex];loader.request(session,++generation,requested);return;}loading=false;autoStopped=true;controls.clear();}
 bool resolve(){
  if(!catalog)return true;candidates.clear();candidateIndex=0;
  if(folderRequest){candidates=catalog->inFolder(requestedFolder);}
  else if(!requested.empty()&&catalog->find(requested)){candidates.push_back(requested);if(startup){auto folder=catalog->find(requested)->folder;for(const auto& id:catalog->inFolder(folder))if(id!=requested)candidates.push_back(id);}}
  else if(startup){for(const auto& entry:catalog->entries())candidates.push_back(entry.id);}
  if(candidates.empty()){loading=false;error="Unknown or empty ISF selection";loader.request(session,generation,"");return false;}
  requested=candidates[0];loader.request(session,generation,requested);return true;
 }
 bool begin(std::string id,bool folder){controlsAllowed=false;++generation;controls.clear();candidates.clear();candidateIndex=0;startup=false;autoStopped=false;loading=true;error.clear();folderRequest=folder;if(folder)requestedFolder=std::move(id);else requested=std::move(id);return resolve();}
 template<class T>void control(const T& command){
  if(loading){if(controls.size()<64)controls.emplace_back(command);else logWarn("ISF pending control limit reached");return;}
  if(!controlsAllowed||!runtime||!parameters)return;std::string why;auto proposed=*parameters;bool valid;
  if constexpr(std::is_same_v<T,cmd::IsfParam>)valid=proposed.set(command,why);else valid=proposed.trigger(command.name,why);
  if(!valid){logWarn("ISF: "+why);return;}auto size=dimensions();if(!runtime->update(proposed,size.first,size.second,why,prepared)){logWarn("ISF: "+why);return;}*parameters=std::move(proposed);dirty=true;
 }
 void navigate(int direction,bool shuffled){
  std::string base=loading?requested:selected.preset;std::string folder=selected.playlist;
  if(catalog){if(auto entry=catalog->find(base))folder=entry->folder;auto ids=catalog->inFolder(folder);if(ids.empty()){begin("",false);return;}
   auto found=std::find(ids.begin(),ids.end(),base);size_t index=found==ids.end()?0:size_t(found-ids.begin());
   if(shuffled&&ids.size()>1){std::uniform_int_distribution<size_t> pick(1,ids.size()-1);index=(index+pick(random))%ids.size();}else index=(index+ids.size()+direction)%ids.size();
   auto id=ids[index];begin(id,false);
   // Automatic and navigation failures may try each eligible shader once.
   candidates.clear();for(size_t n=0;n<ids.size();++n)candidates.push_back(ids[(index+n)%ids.size()]);candidateIndex=0;
  }
 }
 void maintenance(){
  auto event=loader.poll(session);if(!event)return;
  if(event->kind==IsfLoadKind::Catalog){catalog=event->catalog;for(const auto& warning:catalog->warnings())logWarn(warning);if(loading)resolve();return;}
  if(!loading||event->generation!=generation)return;
  if(event->kind==IsfLoadKind::Failed){fail(event->error);return;}
  if(event->kind!=IsfLoadKind::Source)return;
  try{
   auto size=dimensions();auto candidate=std::make_unique<IsfRuntime>(context,budget,event->source,size.first,size.second);auto values=std::make_unique<IsfParameters>(event->source->document.inputs);values->initializeDefaults(candidate->defaults());
   for(const auto& control:controls)std::visit([&](const auto& c){std::string why;bool ok;if constexpr(std::is_same_v<std::decay_t<decltype(c)>,cmd::IsfParam>)ok=values->set(c,why);else ok=values->trigger(c.name,why);if(!ok)logWarn("ISF: "+why);},control);
   candidate->draw(*values,audio,0,0,0);runtime=std::move(candidate);parameters=std::move(values);lastGoodParameters=std::make_unique<IsfParameters>(*parameters);controlsAllowed=true;selected={catalog->find(event->source->document.id)->folder,event->source->document.id};loading=false;startup=false;prepared=true;dirty=false;time=0;elapsed=0;frame=0;controls.clear();error.clear();
  }catch(const std::exception& e){fail(requested+": "+e.what());}
 }
};
IsfBackend::IsfBackend(const BackendInit& i,IsfLoader& l,IsfContext& c,IsfBudget& b):impl_(std::make_unique<Impl>(i,l,c,b)){}
IsfBackend::~IsfBackend()=default;
PreparationResult IsfBackend::preparation(){auto& i=*impl_;if(i.runtime)return {PreparationState::Ready,{}};return {i.loading?PreparationState::Pending:PreparationState::Failed,i.error};}
void IsfBackend::maintenance(){impl_->maintenance();}
void IsfBackend::addPcm(const float* p,size_t n){impl_->audio.append(p,n);if(impl_->prepared)impl_->dirty=true;}
void IsfBackend::clearAudioHistory(){impl_->audio.clear();if(impl_->prepared)impl_->dirty=true;}
void IsfBackend::render(float dt){auto& i=*impl_;if(!i.runtime)return;double elapsed=std::isfinite(dt)?std::max(0.0,double(dt)):0;
 if(i.prepared){
  if(i.dirty)try{i.runtime->draw(*i.parameters,i.audio,0,0,0);}catch(const IsfFrameRejected& e){logWarn(e.what());*i.parameters=*i.lastGoodParameters;}
  i.runtime->present(i.width,i.height);i.prepared=false;i.dirty=false;i.frame=1;i.parameters->presented();*i.lastGoodParameters=*i.parameters;return;
 }
 try{i.runtime->draw(*i.parameters,i.audio,i.time+elapsed,elapsed,i.frame);}
 catch(const IsfFrameRejected& e){logWarn(e.what());*i.parameters=*i.lastGoodParameters;i.runtime->present(i.width,i.height);return;}
 i.runtime->present(i.width,i.height);i.time+=elapsed;++i.frame;i.parameters->presented();*i.lastGoodParameters=*i.parameters;i.elapsed+=elapsed;
 if(!i.loading&&!i.autoStopped&&!i.settings.locked&&i.elapsed>=i.settings.duration){i.elapsed=0;i.navigate(1,i.settings.shuffle);}
}
void IsfBackend::resize(int w,int h){if(w<=0||h<=0)return;auto& i=*impl_;i.width=w;i.height=h;if(i.runtime){auto size=i.dimensions();std::string why;if(!i.runtime->update(*i.parameters,size.first,size.second,why,i.prepared))logWarn("ISF resize: "+why);else if(i.prepared)i.dirty=true;}}
void IsfBackend::setSettings(const PlaybackSettings& s){impl_->settings=s;}
void IsfBackend::next(){impl_->navigate(1,false);}void IsfBackend::prev(){impl_->navigate(-1,false);}void IsfBackend::random(){impl_->navigate(1,true);}
bool IsfBackend::selectPreset(const std::string& id){bool valid=validIsfId(id);bool accepted=impl_->begin(valid?id:"",false);return valid&&accepted;}
bool IsfBackend::selectPlaylist(const std::string& folder){bool valid=folder=="."||validIsfId(folder);bool accepted=impl_->begin(valid?folder:"",true);return valid&&accepted;}
VisualSelection IsfBackend::selection()const{return impl_->selected;}
void IsfBackend::setParameter(const cmd::IsfParam& c){impl_->control(c);}void IsfBackend::triggerEvent(const cmd::IsfEvent& c){impl_->control(c);}
}
