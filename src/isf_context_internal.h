#pragma once
#include "isf_context.h"
#include "VVGL.hpp"
namespace sideshow {
struct IsfContext::Impl {
    VVGL::GLContextRef context;
    VVGL::GLBufferPoolRef pool;
};
}
