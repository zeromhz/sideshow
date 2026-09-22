#pragma once

#include "gl.h"

namespace sideshow {

// Multiplies whatever is in the default framebuffer by a brightness factor, using a
// fullscreen triangle with blend func (ZERO, SRC_ALPHA). Needs a current GL context.
class BrightnessPass {
public:
    BrightnessPass();  // throws std::runtime_error if the shaders fail to build
    ~BrightnessPass();
    BrightnessPass(const BrightnessPass&) = delete;
    BrightnessPass& operator=(const BrightnessPass&) = delete;

    void apply(float brightness, int width, int height);

private:
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLint brightnessLocation_ = -1;
};

}  // namespace sideshow
