#include "video_types.h"
namespace sideshow {
std::optional<VideoEnd> parseVideoEnd(std::string_view value) {
    if (value == "loop") return VideoEnd::Loop;
    if (value == "hold") return VideoEnd::Hold;
    if (value == "next") return VideoEnd::Next;
    return std::nullopt;
}
std::string_view videoEndName(VideoEnd value) {
    switch (value) {
        case VideoEnd::Loop: return "loop";
        case VideoEnd::Hold: return "hold";
        case VideoEnd::Next: return "next";
    }
    return "loop";
}
bool validVideoId(std::string_view value) {
    if (value.empty() || value.find_first_of(":\\") != value.npos ||
        value.find('\0') != value.npos) return false;
    while (true) {
        const auto slash = value.find('/');
        const auto part = value.substr(0, slash);
        if (part.empty() || part.front() == '.') return false;
        if (slash == value.npos) return true;
        value.remove_prefix(slash + 1);
    }
}
}
