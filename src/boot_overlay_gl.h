#pragma once

#include <string>

#include "fluid_gl.h"

namespace sideshow {

// Draws a line of small pixel-font text in the bottom-right corner of the default framebuffer:
// white on a translucent dark box. Needs a current GL context; main thread only.
class BootOverlay {
public:
    BootOverlay();  // throws std::runtime_error if the shaders fail to build
    ~BootOverlay();
    BootOverlay(const BootOverlay&) = delete;
    BootOverlay& operator=(const BootOverlay&) = delete;

    void setText(const std::string& text);  // re-uploads only when the text changes
    void draw(float alpha, int width, int height);

private:
    fluid_gl::Program program_;
    fluid_gl::VertexArray vao_;
    GLuint texture_ = 0;
    std::string text_;
    int textureWidth_ = 0;
    int textureHeight_ = 0;
};

}  // namespace sideshow
