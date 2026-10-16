#pragma once
#include <optional>
#include <string_view>
namespace sideshow {
enum class VisualMode { Milkdrop, Fluid };
std::optional<VisualMode> parseVisualMode(std::string_view value);
std::string_view visualModeName(VisualMode mode);
}
