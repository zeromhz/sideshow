#include "isf_context_internal.h"
#include <stdexcept>
namespace sideshow {
IsfContext::IsfContext():impl_(std::make_unique<Impl>()) {
#ifdef __APPLE__
    auto native=CGLGetCurrentContext();
    if(!native) throw std::runtime_error("ISF requires a current CGL context");
    impl_->context=VVGL::CreateGLContextRefUsing(native,native,CGLGetPixelFormat(native));
#else
    auto native=eglGetCurrentContext();
    if(native==EGL_NO_CONTEXT) throw std::runtime_error("ISF requires a current EGL context");
    impl_->context=VVGL::CreateGLContextRefUsing(eglGetCurrentDisplay(),eglGetCurrentSurface(EGL_DRAW),native,native);
#endif
    impl_->pool=std::make_shared<VVGL::GLBufferPool>(impl_->context);
}
IsfContext::~IsfContext() { impl_->pool->prepareToBeDeleted(); }
IsfContext::Impl& IsfContext::impl() { return *impl_; }
}
