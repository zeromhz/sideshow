#include "fluid_playback.h"
#include <cmath>
namespace sideshow {
FluidPlayback::FluidPlayback(std::string_view preset, PlaybackSettings settings, uint32_t seed)
    : settings_(settings), rng_(seed) { select(preset); }
bool FluidPlayback::select(std::string_view name) {
    const auto* p = findFluidPreset(name);
    if (!p) return false;
    index_ = static_cast<size_t>(p - fluidPresets().data()); elapsed_ = 0; return true;
}
void FluidPlayback::random() {
    index_ = (index_ + 1 + std::uniform_int_distribution<size_t>(0,fluidPresets().size()-2)(rng_)) % fluidPresets().size();
    elapsed_ = 0;
}
void FluidPlayback::next() {
    if (settings_.shuffle) { random(); return; }
    index_ = (index_ + 1) % fluidPresets().size(); elapsed_ = 0;
}
void FluidPlayback::prev() {
    if (settings_.shuffle) { random(); return; }
    index_ = (index_ + fluidPresets().size()-1) % fluidPresets().size(); elapsed_ = 0;
}
void FluidPlayback::update(float dt) {
    if (settings_.locked || !std::isfinite(dt) || dt <= 0) return;
    elapsed_ += dt;
    if (elapsed_ >= settings_.duration) next();
}
}
