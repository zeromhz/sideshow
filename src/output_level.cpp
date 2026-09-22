#include "output_level.h"

#include <algorithm>
#include <cmath>

namespace sideshow {

OutputLevel::OutputLevel(float initial, float curve, float smoothingMs)
    : target_(std::clamp(initial, 0.0f, 1.0f)),
      smoothed_(target_),
      curve_(curve),
      tauSeconds_(smoothingMs / 1000.0f) {}

void OutputLevel::setTarget(float value) { target_ = std::clamp(value, 0.0f, 1.0f); }

void OutputLevel::setBlackout(bool on) { blackout_ = on; }

void OutputLevel::update(float dtSeconds) {
    if (tauSeconds_ <= 0.0f) {
        smoothed_ = target_;
        return;
    }
    if (dtSeconds <= 0.0f) return;
    const float alpha = 1.0f - std::exp(-dtSeconds / tauSeconds_);
    smoothed_ += (target_ - smoothed_) * alpha;
}

float OutputLevel::brightness() const {
    if (blackout_) return 0.0f;
    return std::pow(smoothed_, curve_);
}

}  // namespace sideshow
