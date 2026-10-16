#pragma once
#include "fluid_presets.h"
#include <random>
#include <string>
namespace sideshow {
struct FluidSplat { float x, y, dx, dy; Color color; };
class FluidMotion {
public:
    explicit FluidMotion(uint32_t seed) : rng_(seed) {}
    void addPcm(const float* stereo, size_t frames);
    std::vector<FluidSplat> update(float dtSeconds, float sensitivity, float ambientInterval,
                                 const FluidPreset& preset);
    std::vector<FluidSplat> seed(const FluidPreset& preset);
private:
    float unit();
    Color color(const FluidPreset& preset);
    FluidSplat splat(const FluidPreset& preset);
    std::mt19937 rng_;
    float energy_ = 0, baseline_ = 0, envelope_ = 0;
    float cooldown_ = 1, ambient_ = 0, colorElapsed_ = 100;
    bool fresh_ = false;
    Color color_{1,0,0};
    std::string preset_;
};
}
