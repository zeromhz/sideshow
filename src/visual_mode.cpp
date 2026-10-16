#include "visual_mode.h"
namespace sideshow {
std::optional<VisualMode> parseVisualMode(std::string_view value) {
    if (value == "milkdrop") return VisualMode::Milkdrop;
    if (value == "fluid") return VisualMode::Fluid;
    if (value == "isf") return VisualMode::Isf;
    if (value == "video") return VisualMode::Video;
    return std::nullopt;
}
std::string_view visualModeName(VisualMode mode) {
    if (mode == VisualMode::Isf) return "isf";
    if (mode == VisualMode::Video) return "video";
    return mode == VisualMode::Fluid ? "fluid" : "milkdrop";
}
}
