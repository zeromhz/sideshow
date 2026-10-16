#include "renderer.h"

#include <stdexcept>

#include "gl.h"
#include "log.h"

namespace sideshow {

Renderer::Renderer(int width, int height, int fps, const Config::Presets& presets)
    : width_(width), height_(height) {
    projectm_ = projectm_create();
    if (!projectm_) throw std::runtime_error("projectm_create failed (no usable GL context?)");

    projectm_set_window_size(projectm_, static_cast<size_t>(width), static_cast<size_t>(height));
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
    if (projectm_) projectm_destroy(projectm_);
}

void Renderer::resize(int width, int height) {
    width_ = width;
    height_ = height;
    projectm_set_window_size(projectm_, static_cast<size_t>(width), static_cast<size_t>(height));
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
}

void Renderer::render(float brightness) {
    renderFrame();
    brightness_.apply(brightness, width_, height_);
}

void Renderer::setBeatSensitivity(float value) { projectm_set_beat_sensitivity(projectm_, value); }

void Renderer::setHardCutEnabled(bool on) { projectm_set_hard_cut_enabled(projectm_, on); }

void Renderer::setPresetDuration(double seconds) { projectm_set_preset_duration(projectm_, seconds); }

}  // namespace sideshow
