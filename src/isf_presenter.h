#pragma once
#include "fluid_gl.h"
namespace sideshow {
class IsfPresenter {
public:
 IsfPresenter();void present(GLuint texture,int width,int height,int drawableWidth,int drawableHeight);
private:fluid_gl::Program program_;fluid_gl::VertexArray vao_;
};
}
