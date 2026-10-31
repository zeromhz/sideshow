#include "isf_runtime.h"
#include "isf_context_internal.h"
#include "isf_presenter.h"
#include "VVISF.hpp"
#include "ISFPassTarget.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
namespace sideshow {
namespace {
// 32-bit float textures can only be sampled with linear filtering where the driver supports
// OES_texture_float_linear. The Pi's V3D driver does not: an RGBA32F texture with GL_LINEAR (how
// VVGL creates float textures) is incomplete there and samples as zero, which silenced every
// shader's audio input and blacked out FLOAT passes. Where it is missing, float targets and audio
// textures use RGBA16F instead, which V3D filters. SIDESHOW_ISF_HALF_FLOAT=1 forces this (tests).
bool halfFloatTargets(){
 static const bool unsupported=[]{GLint n=0;glGetIntegerv(GL_NUM_EXTENSIONS,&n);
  for(GLint i=0;i<n;++i){auto e=reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS,GLuint(i)));if(e&&std::strcmp(e,"GL_OES_texture_float_linear")==0)return false;}
  return true;}();
 const char* forced=std::getenv("SIDESHOW_ISF_HALF_FLOAT");
 return unsupported||(forced&&std::strcmp(forced,"1")==0);
}
VVGL::GLBufferRef createHalfFloatTex(const VVGL::Size& size,const VVGL::GLBufferPoolRef& pool){
 VVGL::GLBuffer::Descriptor desc;
 desc.type=VVGL::GLBuffer::Type_Tex;desc.target=VVGL::GLBuffer::Target_2D;
 desc.internalFormat=static_cast<VVGL::GLBuffer::InternalFormat>(GL_RGBA16F);
 desc.pixelFormat=VVGL::GLBuffer::PF_RGBA;desc.pixelType=static_cast<VVGL::GLBuffer::PixelType>(GL_HALF_FLOAT);
 desc.cpuBackingType=VVGL::GLBuffer::Backing_None;desc.gpuBackingType=VVGL::GLBuffer::Backing_Internal;
 auto texture=pool->createBufferRef(desc,size,nullptr,VVGL::Size(),true);
 if(texture)texture->parentBufferPool=pool;
 return texture;
}
using namespace VVGL;using namespace VVISF;
struct Guard {
 fluid_gl::StateGuard state;
 GLint active=0,unpack=0,pack=0,array=0,renderbuffer=0;
 std::array<GLint,4> pixel{};std::vector<GLint> textures,samplers;
 Guard(){glGetIntegerv(GL_ACTIVE_TEXTURE,&active);GLint n=0;glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,&n);textures.resize(n);samplers.resize(n);
  for(int i=0;i<n;++i){glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);glGetIntegerv(GL_SAMPLER_BINDING,&samplers[i]);glBindSampler(i,0);}
  glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array);glGetIntegerv(GL_RENDERBUFFER_BINDING,&renderbuffer);
  GLenum keys[]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS};for(int i=0;i<4;++i){glGetIntegerv(keys[i],&pixel[i]);glPixelStorei(keys[i],i==0?1:0);}
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);fluid_gl::resetState();
 }
 ~Guard(){for(size_t i=0;i<textures.size();++i){glActiveTexture(GL_TEXTURE0+GLenum(i));glBindTexture(GL_TEXTURE_2D,textures[i]);glBindSampler(GLuint(i),samplers[i]);}glActiveTexture(active);
  GLenum keys[]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS};for(int i=0;i<4;++i)glPixelStorei(keys[i],pixel[i]);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);glBindBuffer(GL_PIXEL_PACK_BUFFER,pack);glBindBuffer(GL_ARRAY_BUFFER,array);glBindRenderbuffer(GL_RENDERBUFFER,renderbuffer);
 }
};
struct HostScene:VVISF::ISFScene {
 using ISFScene::ISFScene;
 void discardFrame(){_renderTarget={};}
};
struct Pool {GLBufferPoolRef value;~Pool(){if(value)value->prepareToBeDeleted();}};
ISFVal runtimeValue(const IsfValue& v){return std::visit([](const auto& x)->ISFVal{using T=std::decay_t<decltype(x)>;if constexpr(std::is_same_v<T,bool>)return ISFBoolVal(x);else if constexpr(std::is_same_v<T,int32_t>)return ISFLongVal(x);else if constexpr(std::is_same_v<T,double>)return ISFFloatVal(x);else if constexpr(std::is_same_v<T,std::array<double,2>>)return ISFPoint2DVal(x[0],x[1]);else return ISFColorVal(x[0],x[1],x[2],x[3]);},v);}
IsfValue hostValue(ISFVal v,IsfInputKind kind){switch(kind){case IsfInputKind::Bool:return v.getBoolVal();case IsfInputKind::Long:return v.getLongVal();case IsfInputKind::Float:return v.getDoubleVal();case IsfInputKind::Point2D:return std::array<double,2>{v.getPointValByIndex(0),v.getPointValByIndex(1)};case IsfInputKind::Color:return std::array<double,4>{v.getColorValByChannel(0),v.getColorValByChannel(1),v.getColorValByChannel(2),v.getColorValByChannel(3)};default:return false;}}
void checkGl(){auto e=glGetError();if(e==GL_OUT_OF_MEMORY)throw IsfFrameRejected("ISF GL allocation failed");if(e!=GL_NO_ERROR)throw std::runtime_error("ISF GL operation failed");}
std::runtime_error error(const ISFErr& e){std::string msg=e.general+": "+e.specific;for(const auto& d:e.details)if(d.first.find("ErrLog")!=std::string::npos)msg+="\n"+d.first+": "+d.second.substr(0,4096);return std::runtime_error(msg);}
}
struct IsfRuntime::Impl {
 Pool pool;std::shared_ptr<const IsfSource> source;std::shared_ptr<HostScene> scene;VVISF::ISFDocRef doc;
 std::unique_ptr<IsfPresenter> presenter;std::map<std::string,IsfValue> defaults,committed;
 VVGL::GLBufferRef output;std::map<std::string,VVGL::GLBufferRef> imports,audio;
 int width,height,outputWidth,outputHeight;bool drawn=false;
 struct Snapshot {int width,height;std::map<std::string,IsfValue> values;std::vector<VVGL::Size> sizes;std::vector<VVGL::GLBufferRef> buffers;};
 std::optional<Snapshot> pending;bool pendingCleared=false;
 Snapshot snapshot(){Snapshot s{width,height,committed,{},{}};for(const auto& t:targets()){s.sizes.push_back(t->targetSize());s.buffers.push_back(t->buffer());}return s;}
 void restore(const Snapshot& s){width=s.width;height=s.height;committed=s.values;values(committed);auto ts=targets();for(size_t n=0;n<ts.size();++n){ts[n]->restoreHostSize(s.sizes[n]);ts[n]->setBuffer(s.buffers[n]);}}
 void rollback(){scene->discardFrame();if(pending){restore(*pending);pending.reset();}pendingCleared=false;pool.value->purge();}

 Impl(IsfContext& context,IsfBudget& budget,std::shared_ptr<const IsfSource> s,int w,int h):source(std::move(s)),width(w),height(h),outputWidth(w),outputHeight(h){
  Guard guard;auto native=context.impl().context;pool.value=std::make_shared<VVGL::GLBufferPool>(native);auto owner=budget.newOwner();GLint max=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&max);
  pool.value->reserveStorage=[&budget,owner,max](const VVGL::GLBuffer::Descriptor& d,const VVGL::Size& size)->std::shared_ptr<void>{
   if(d.type!=VVGL::GLBuffer::Type_Tex)return {};
   if(d.target!=VVGL::GLBuffer::Target_2D||!std::isfinite(size.width)||!std::isfinite(size.height)||size.width<1||size.height<1||size.width>std::min(max,4096)||size.height>std::min(max,4096))throw std::runtime_error("ISF texture dimensions exceed limits");
   size_t bpp=d.internalFormat==VVGL::GLBuffer::IF_RGBA32F?16:4;
   try{return std::make_shared<IsfBudget::Lease>(budget.reserve(owner,isfTextureBytes(int(std::ceil(size.width)),int(std::ceil(size.height)),bpp)));}
   catch(const std::exception& e){throw IsfFrameRejected(e.what());}
  };
  scene=std::make_shared<HostScene>(native);scene->setPrivatePool(pool.value);scene->setThrowExceptions(true);scene->hostFrame=true;
  auto copier=std::make_shared<VVGL::GLTexToTexCopier>(native);copier->setPrivatePool(pool.value);scene->setPrivateCopier(copier);
  for(const auto& image:source->images){auto texture=make(image.width,image.height,false);glBindTexture(GL_TEXTURE_2D,texture->name);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,image.width,image.height,GL_RGBA,GL_UNSIGNED_BYTE,image.rgba.data());checkGl();texture->flipped=true;imports.emplace(image.name,texture);}
  scene->validatedImport=[this](const std::string& name){auto it=imports.find(name);if(it==imports.end())throw std::runtime_error("Missing validated ISF import "+name);return it->second;};
  doc=std::make_shared<VVISF::ISFDoc>(source->document.fragment,source->document.vertex.empty()?VVISF::ISFVertPassthru_GL2:source->document.vertex,"",scene.get(),true);scene->useDoc(doc);
  for(const auto& input:source->document.inputs){if(input.kind==IsfInputKind::Event)continue;if(input.kind==IsfInputKind::Audio||input.kind==IsfInputKind::AudioFFT){audio.emplace(input.name,make(int(input.audioWidth),2,true));continue;}auto attr=doc->input(input.name);if(!attr)throw std::runtime_error("Missing ISF runtime input");defaults[input.name]=hostValue(attr->currentVal(),input.kind);}
  GLint units=0;glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&units);if(imports.size()+audio.size()+source->document.passes.size()>size_t(units))throw std::runtime_error("ISF shader exceeds texture unit limit");
  presenter=std::make_unique<IsfPresenter>();output=make(w,h,false);checkGl();
 }
 // VVISF clears every render target before drawing into it, so only buffers that are read before
 // they are drawn (replacement history) need clearing here. Skipping the others saves a
 // full-size clear per buffer per frame, which a tiled GPU flushes to memory as its own job.
 VVGL::GLBufferRef make(int w,int h,bool floating,bool clear=true){
  auto texture=floating?(halfFloatTargets()?createHalfFloatTex(VVGL::Size(w,h),pool.value):VVGL::CreateRGBAFloatTex(VVGL::Size(w,h),true,pool.value)):VVGL::CreateRGBATex(VVGL::Size(w,h),true,pool.value);
  auto fbo=VVGL::CreateFBO(true,pool.value);glBindFramebuffer(GL_FRAMEBUFFER,fbo->name);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture->name,0);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("ISF requested texture format is not renderable");if(clear){const GLfloat black[]={0,0,0,0};glClearBufferfv(GL_COLOR,0,black);}checkGl();return texture;
 }
 std::vector<VVISF::ISFPassTargetRef> targets(){auto result=doc->persistentPassTargets();auto temps=doc->tempPassTargets();result.insert(result.end(),temps.begin(),temps.end());return result;}
 struct OutputSpec {int width,height;bool floating;};
 OutputSpec outputSpec(int w,int h){
  const auto& passes=source->document.passes;
  if(passes.empty()||passes.back().target.empty())return {w,h,false};
  const auto& name=passes.back().target;
  auto target=doc->persistentPassTargetForKey(name);
  if(!target)target=doc->tempPassTargetForKey(name);
  if(!target)throw std::runtime_error("Missing final ISF target");
  auto size=target->targetSize();return {int(size.width),int(size.height),target->floatFlag()};
 }
 void values(const std::map<std::string,IsfValue>& v){for(const auto& [name,value]:v)scene->setValueForInputNamed(runtimeValue(value),name);}
 bool update(const IsfParameters& parameters,int w,int h,std::string& failure,bool resetHistory=false){
  if(w<=0||h<=0)return true;resetHistory=resetHistory&&!pendingCleared;Guard guard;auto before=snapshot();auto ts=targets();
  try{
   values(parameters.values());doc->evalBufferDimensionsWithRenderSize(VVGL::Size(w,h),false);
   bool resize=w!=width||h!=height;for(size_t n=0;n<ts.size();++n)resize=resize||ts[n]->targetSize()!=before.sizes[n];
   if(resize)pool.value->purge();
   std::vector<VVGL::GLBufferRef> replacement(ts.size()),warm;
   // Hold every destination concurrently before committing. This admits the full
   // ping-pong peak, including old history and a cleared first-frame redraw.
   for(size_t n=0;n<ts.size();++n){auto size=ts[n]->targetSize();
    if(resize||resetHistory||!ts[n]->buffer())replacement[n]=make(int(size.width),int(size.height),ts[n]->floatFlag());
    warm.push_back(make(int(size.width),int(size.height),ts[n]->floatFlag(),false));
   }
   // Unnamed intermediate passes also allocate full-size RGBA destinations.
   const auto& passes=source->document.passes;
   for(size_t n=0;n+1<passes.size();++n)if(passes[n].target.empty())warm.push_back(make(w,h,false,false));
   // A named final pass uses its own destination (already warmed above).
   // Unnamed output needs separate ping-pong storage across a resize.
   if(passes.empty()||passes.back().target.empty()){
    warm.push_back(make(w,h,false,false));
    if(w!=outputWidth||h!=outputHeight)warm.push_back(make(w,h,false,false));
   }
   if(!pending)pending=before;
   for(size_t n=0;n<ts.size();++n)if(replacement[n])ts[n]->setBuffer(replacement[n]);
   pendingCleared=pendingCleared||resetHistory;
   width=w;height=h;committed=parameters.values();failure.clear();return true;
  }catch(const VVISF::ISFErr& e){failure=error(e).what();}catch(const std::exception& e){failure=e.what();}
  restore(before);pool.value->purge();return false;
 }

};
IsfRuntime::IsfRuntime(IsfContext& c,IsfBudget& b,std::shared_ptr<const IsfSource> s,int w,int h){Guard guard;try{impl_=std::make_unique<Impl>(c,b,std::move(s),w,h);}catch(const VVISF::ISFErr& e){throw error(e);}}
IsfRuntime::~IsfRuntime(){Guard guard;impl_.reset();}
const std::map<std::string,IsfValue>& IsfRuntime::defaults()const{return impl_->defaults;}
bool IsfRuntime::update(const IsfParameters& p,int w,int h,std::string& error,bool resetHistory){return impl_->update(p,w,h,error,resetHistory);}
void IsfRuntime::draw(const IsfParameters& p,const IsfAudio& a,double time,double delta,uint64_t frame){
 auto& r=*impl_;Guard guard;std::string failure;
 if(!r.update(p,r.width,r.height,failure,frame==0&&r.drawn&&!r.pendingCleared)){r.rollback();throw IsfFrameRejected(failure);}
 try{
  for(const auto& input:r.source->document.inputs){if(input.kind==IsfInputKind::Event){r.scene->setValueForInputNamed(VVISF::ISFEventVal(p.events().count(input.name)!=0),input.name);continue;}
   if(input.kind!=IsfInputKind::Audio&&input.kind!=IsfInputKind::AudioFFT)continue;auto samples=input.kind==IsfInputKind::Audio?a.waveform(input.audioWidth):a.spectrum(input.audioWidth);std::vector<float> rgba(samples.samples.size()*4);for(size_t i=0;i<samples.samples.size();++i){rgba[4*i]=rgba[4*i+1]=rgba[4*i+2]=samples.samples[i];rgba[4*i+3]=1;}
   auto texture=r.audio.at(input.name);glBindTexture(GL_TEXTURE_2D,texture->name);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,int(input.audioWidth),2,GL_RGBA,GL_FLOAT,rgba.data());r.scene->setBufferForAudioInputKey(texture,input.name);
  }
  auto outputSpec=r.outputSpec(r.width,r.height);auto next=r.make(outputSpec.width,outputSpec.height,outputSpec.floating,false);r.scene->hostTime=time;r.scene->hostDelta=delta;r.scene->hostIndex=frame;r.scene->renderToBuffer(next,VVGL::Size(r.width,r.height),time);checkGl();r.output=next;r.outputWidth=outputSpec.width;r.outputHeight=outputSpec.height;r.drawn=true;r.pending.reset();r.pendingCleared=false;
 }catch(const VVISF::ISFErr& e){r.rollback();throw error(e);}catch(...){r.rollback();throw;}
 r.pool.value->housekeeping();
}
void IsfRuntime::present(int w,int h){Guard guard;impl_->presenter->present(impl_->output->name,impl_->outputWidth,impl_->outputHeight,w,h);checkGl();}
}
