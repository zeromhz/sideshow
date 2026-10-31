#pragma once

#include <projectM-4/projectM.h>

#include "config.h"
#include "display_mode.h"
#include "gl.h"

namespace sideshow {

// Owns the projectM instance. Main thread only; needs a current GL context.
//
// With presets.render_scale < 1, projectM renders at that fraction of the screen size (same
// aspect ratio) and the frame is scaled up to fill the screen. projectM 4.1 always draws its
// final frame into the default framebuffer at its own size, so the frame lands in the
// bottom-left corner of the back buffer; it is copied into a small renderbuffer and blitted
// back over the whole screen with linear filtering.
class Renderer {
public:
    Renderer(int width, int height, int fps, const Config::Presets& presets);  // throws
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    projectm_handle handle() const { return projectm_; }

    void resize(int width, int height);
    void addPcm(const float* interleavedStereo, unsigned int frames);
    void renderFrame();

    void setBeatSensitivity(float value);
    void setHardCutEnabled(bool on);
    void setPresetDuration(double seconds);

    RenderSize internalSize() const { return internal_; }

private:
    void configureSize(int width, int height);
    void releaseTarget();

    projectm_handle projectm_ = nullptr;
    double scale_;
    int width_ = 0;
    int height_ = 0;
    RenderSize internal_{0, 0};
    GLuint framebuffer_ = 0;   // 0 = render straight to the screen at full size
    GLuint colorBuffer_ = 0;
};

}  // namespace sideshow
