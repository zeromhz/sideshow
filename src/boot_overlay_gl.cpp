#include "boot_overlay_gl.h"

#include "boot_overlay.h"

namespace sideshow {

BootOverlay::BootOverlay()
    : program_(R"(
out vec2 uv;
void main(){vec2 p=vec2(float((gl_VertexID&1)<<2)-1.0,float((gl_VertexID&2)<<1)-1.0);uv=(p+1.0)*0.5;gl_Position=vec4(p,0,1);}
)",
               R"(
in vec2 uv;uniform sampler2D image;uniform float alpha;out vec4 color;
// Bitmap row 0 is the top of the text; flip so it reads upright.
void main(){float t=texture(image,vec2(uv.x,1.0-uv.y)).r;color=vec4(vec3(t),mix(0.55,1.0,t)*alpha);}
)") {
    glGenTextures(1, &texture_);
}

BootOverlay::~BootOverlay() {
    if (texture_) glDeleteTextures(1, &texture_);
}

void BootOverlay::setText(const std::string& text) {
    if (text == text_ && textureWidth_ > 0) return;
    text_ = text;
    const TextBitmap bitmap = rasterizeText(text, 2);
    textureWidth_ = bitmap.width;
    textureHeight_ = bitmap.height;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, bitmap.width, bitmap.height, 0, GL_RED, GL_UNSIGNED_BYTE,
                 bitmap.pixels.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void BootOverlay::draw(float alpha, int width, int height) {
    if (alpha <= 0.0f || textureWidth_ <= 0 || width <= 0 || height <= 0) return;
    const int scale = bootOverlayScale(height);
    const int w = textureWidth_ * scale;
    const int h = textureHeight_ * scale;
    const int margin = 4 * scale;  // from the screen's bottom-right corner
    fluid_gl::resetState();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(width - w - margin, margin, w, h);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    program_.use();
    vao_.bind();
    program_.texture("image", texture_, 0);
    program_.one("alpha", alpha);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);
    glViewport(0, 0, width, height);
}

}  // namespace sideshow
