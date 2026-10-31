#include "preset_performance.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sideshow {

PresetPerformance::PresetPerformance(double targetFps, double reportSeconds)
    : latePeriod_(1.5 / std::max(1.0, targetFps)), reportSeconds_(reportSeconds) {}

void PresetPerformance::frame(double interval) {
    if (!std::isfinite(interval) || interval < 0) return;
    total_ += interval;
    slowest_ = std::max(slowest_, interval);
    ++frames_;
    if (interval > latePeriod_) ++late_;
}

std::optional<std::string> PresetPerformance::update(const std::string& preset) {
    std::optional<std::string> line;
    if (preset != preset_) {
        if (!preset_.empty() && total_ >= 2) line = summary();
        preset_ = preset;
        reset();
    } else if (total_ >= reportSeconds_) {
        line = summary();
        reset();
    }
    return line;
}

std::string PresetPerformance::summary() const {
    char numbers[160];
    std::snprintf(numbers, sizeof(numbers), " %.1f fps, %d%% late, slowest %d ms over %d s",
                  frames_ / total_, static_cast<int>(std::lround(100.0 * late_ / std::max(1L, frames_))),
                  static_cast<int>(std::lround(slowest_ * 1000)), static_cast<int>(std::lround(total_)));
    return "perf: " + preset_ + numbers;
}

void PresetPerformance::reset() {
    total_ = slowest_ = 0;
    frames_ = late_ = 0;
}

}  // namespace sideshow
