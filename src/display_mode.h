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

}  // namespace sideshow
