#pragma once
#include <optional>
#include <cstddef>
#include <string_view>
namespace sideshow {
enum class VisualMode { Milkdrop, Fluid, Video };
inline constexpr std::size_t kVisualModeCount = 3;
std::optional<VisualMode> parseVisualMode(std::string_view value);
std::string_view visualModeName(VisualMode mode);
}
