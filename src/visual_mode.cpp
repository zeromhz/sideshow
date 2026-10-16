#include "visual_mode.h"
namespace sideshow {
std::optional<VisualMode> parseVisualMode(std::string_view value) {
    if (value == "milkdrop") return VisualMode::Milkdrop;
    if (value == "fluid") return VisualMode::Fluid;
    return std::nullopt;
}
std::string_view visualModeName(VisualMode mode) {
    return mode == VisualMode::Fluid ? "fluid" : "milkdrop";
}
}
