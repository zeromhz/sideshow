#pragma once

#include <projectM-4/projectM.h>

#include "brightness_pass.h"
#include "config.h"

namespace sideshow {

// Owns the projectM instance. Main thread only; needs a current GL context.
class Renderer {
public:
    Renderer(int width, int height, int fps, const Config::Presets& presets);  // throws
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    projectm_handle handle() const { return projectm_; }

    void resize(int width, int height);
    void addPcm(const float* interleavedStereo, unsigned int frames);
    void render(float brightness);

    void setBeatSensitivity(float value);
    void setHardCutEnabled(bool on);
    void setPresetDuration(double seconds);

private:
    projectm_handle projectm_ = nullptr;
    BrightnessPass brightness_;
    int width_;
    int height_;
};

}  // namespace sideshow
