#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace sideshow {

struct DisplayMode {
    int width;
    int height;
    int refreshRate;  // Hz; 0 = unknown
};

// Index of the mode with exactly width x height and the best refresh rate, if any.
std::optional<size_t> findExactMode(const std::vector<DisplayMode>& modes, int width, int height);

// Automatic choice: the native size if it fits within maxWidth x maxHeight, otherwise the
// largest mode that fits, preferring the native aspect ratio. Among modes of the chosen size,
// 50-60 Hz is preferred (60 over 50). Returns nullopt if no mode fits the cap.
std::optional<size_t> pickAutoMode(const std::vector<DisplayMode>& modes, const DisplayMode& native,
                                   int maxWidth, int maxHeight);

struct RenderSize {
    int width;
    int height;
};

// Internal render size for a drawable scaled by `scale` (0 < scale <= 1) in both directions, so
// the aspect ratio is kept: 1920x1080 at 2/3 gives 1280x720. Never smaller than 1x1.
RenderSize scaledRenderSize(int width, int height, double scale);

// Vsync swap interval that shows each frame for whole refreshes without dropping below `fps`:
// floor((refresh + 1) / fps), at least 1 (1 Hz tolerance for 59.94 Hz modes reported as 59). 60 Hz at 30 fps swaps every second vblank; 50 Hz at 30 fps
// swaps every vblank and leaves the cap to the frame limiter. Unknown refresh gives 1.
int swapIntervalFor(int refreshRate, int fps);

}  // namespace sideshow
