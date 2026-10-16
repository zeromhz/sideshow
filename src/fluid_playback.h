#pragma once
#include "fluid_presets.h"
#include "visual_backend.h"
#include <random>
namespace sideshow {
class FluidPlayback {
public:
    FluidPlayback(std::string_view preset, PlaybackSettings settings, uint32_t seed);
    bool select(std::string_view name);
    void next(); void prev(); void random();
    void setSettings(const PlaybackSettings& settings) { settings_ = settings; }
    void update(float dtSeconds);
    const FluidPreset& current() const { return fluidPresets()[index_]; }
private:
    size_t index_ = 0;
    double elapsed_ = 0;
    PlaybackSettings settings_;
    std::mt19937 rng_;
};
}
