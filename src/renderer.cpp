#include "renderer.h"

#include <stdexcept>
#include <string>

#include "log.h"

namespace sideshow {

Renderer::Renderer(int width, int height, int fps, const Config::Presets& presets)
    : scale_(presets.renderScale) {
    projectm_ = projectm_create();
    if (!projectm_) throw std::runtime_error("projectm_create failed (no usable GL context?)");

    configureSize(width, height);
    projectm_set_fps(projectm_, fps);
    projectm_set_aspect_correction(projectm_, true);
    projectm_set_preset_duration(projectm_, presets.duration);
    projectm_set_soft_cut_duration(projectm_, presets.softCutDuration);
    projectm_set_hard_cut_enabled(projectm_, presets.hardcutEnabled);
    projectm_set_beat_sensitivity(projectm_, presets.beatSensitivity);

    const char* texturePaths[] = {presets.textures.c_str()};
    projectm_set_texture_search_paths(projectm_, texturePaths, 1);

    char* version = projectm_get_version_string();
    logInfo(std::string("projectM ") + version);
    projectm_free_string(version);
}

Renderer::~Renderer() {
    releaseTarget();
    if (projectm_) projectm_destroy(projectm_);
}

void Renderer::releaseTarget() {
    if (framebuffer_) glDeleteFramebuffers(1, &framebuffer_);
    if (colorBuffer_) glDeleteRenderbuffers(1, &colorBuffer_);
    framebuffer_ = 0;
    colorBuffer_ = 0;
}

void Renderer::configureSize(int width, int height) {
    width_ = width;
    height_ = height;
    internal_ = scaledRenderSize(width, height, scale_);
    releaseTarget();
    if (internal_.width < width || internal_.height < height) {
        glGenRenderbuffers(1, &colorBuffer_);
        glBindRenderbuffer(GL_RENDERBUFFER, colorBuffer_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, internal_.width, internal_.height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glGenFramebuffers(1, &framebuffer_);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorBuffer_);
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!complete) {
            logWarn("MilkDrop scaled render target unavailable; rendering at full size");
            releaseTarget();
            internal_ = {width, height};
        }
    }
    projectm_set_window_size(projectm_, static_cast<size_t>(internal_.width),
                             static_cast<size_t>(internal_.height));
    logInfo("MilkDrop render " + std::to_string(internal_.width) + "x" +
            std::to_string(internal_.height) + " for screen " + std::to_string(width) + "x" +
            std::to_string(height));
}

void Renderer::resize(int width, int height) {
    if (width == width_ && height == height_) return;
    configureSize(width, height);
}

void Renderer::addPcm(const float* interleavedStereo, unsigned int frames) {
    projectm_pcm_add_float(projectm_, interleavedStereo, frames, PROJECTM_STEREO);
}

void Renderer::renderFrame() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    projectm_opengl_render_frame(projectm_);
    if (!framebuffer_) {
        // projectM resets only the draw binding; leave reads on the screen too.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    // projectM left its frame in the bottom-left internal_ corner of the back buffer. Copy it
    // out, then stretch it over the whole screen. Blits and clears honour the scissor test, so
    // make sure it is off.
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer_);
    glBlitFramebuffer(0, 0, internal_.width, internal_.height, 0, 0, internal_.width,
                      internal_.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    // The stretch overwrites the whole screen. Clearing first tells a tiled GPU (the Pi's V3D)
    // not to reload the old screen contents from memory before drawing over them.
    glClear(GL_COLOR_BUFFER_BIT);
    glBlitFramebuffer(0, 0, internal_.width, internal_.height, 0, 0, width_, height_,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::setBeatSensitivity(float value) { projectm_set_beat_sensitivity(projectm_, value); }

void Renderer::setHardCutEnabled(bool on) { projectm_set_hard_cut_enabled(projectm_, on); }

void Renderer::setPresetDuration(double seconds) { projectm_set_preset_duration(projectm_, seconds); }

}  // namespace sideshow
