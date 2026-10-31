#include "display_mode.h"

#include <algorithm>
#include <cmath>

namespace sideshow {
namespace {

// Lower is better. projectM renders once per vsync, so >60 Hz costs GPU for no benefit.
int refreshPenalty(int hz) {
    if (hz == 0) return 0;                     // unknown: assume fine
    if (hz >= 50 && hz <= 61) return 61 - hz;  // prefer 60 over 50
    if (hz > 61) return 100 + hz;
    return 200 + (50 - hz);
}

bool sameAspect(const DisplayMode& a, const DisplayMode& b) {
    if (a.height == 0 || b.height == 0) return false;
    const double ra = static_cast<double>(a.width) / a.height;
    const double rb = static_cast<double>(b.width) / b.height;
    return std::abs(ra - rb) / rb < 0.01;
}

bool fits(const DisplayMode& m, int maxWidth, int maxHeight) {
    return m.width <= maxWidth && m.height <= maxHeight;
}

}  // namespace

std::optional<size_t> findExactMode(const std::vector<DisplayMode>& modes, int width, int height) {
    std::optional<size_t> best;
    for (size_t i = 0; i < modes.size(); ++i) {
        if (modes[i].width != width || modes[i].height != height) continue;
        if (!best || refreshPenalty(modes[i].refreshRate) < refreshPenalty(modes[*best].refreshRate)) {
            best = i;
        }
    }
    return best;
}

std::optional<size_t> pickAutoMode(const std::vector<DisplayMode>& modes, const DisplayMode& native,
                                   int maxWidth, int maxHeight) {
    if (fits(native, maxWidth, maxHeight)) {
        if (auto index = findExactMode(modes, native.width, native.height)) return index;
    }

    std::optional<size_t> bestSize;
    for (size_t i = 0; i < modes.size(); ++i) {
        const DisplayMode& m = modes[i];
        if (!fits(m, maxWidth, maxHeight)) continue;
        if (!bestSize) {
            bestSize = i;
            continue;
        }
        const DisplayMode& b = modes[*bestSize];
        const bool mAspect = sameAspect(m, native);
        const bool bAspect = sameAspect(b, native);
        if (mAspect != bAspect) {
            if (mAspect) bestSize = i;
            continue;
        }
        if (static_cast<long>(m.width) * m.height > static_cast<long>(b.width) * b.height) {
            bestSize = i;
        }
    }
    if (!bestSize) return std::nullopt;
    return findExactMode(modes, modes[*bestSize].width, modes[*bestSize].height);
}

RenderSize scaledRenderSize(int width, int height, double scale) {
    if (!(scale > 0.0) || scale > 1.0) scale = 1.0;
    return {std::max(1, static_cast<int>(std::lround(width * scale))),
            std::max(1, static_cast<int>(std::lround(height * scale)))};
}

int refreshesPerFrame(int refreshRate, int fps) {
    if (refreshRate <= 0 || fps <= 0) return 1;
    // +1 Hz tolerance: 59.94 Hz modes can report 59 and should still swap every second vblank.
    return std::max(1, (refreshRate + 1) / fps);
}

}  // namespace sideshow
