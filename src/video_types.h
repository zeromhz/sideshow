#pragma once
#include <optional>
#include <string_view>
namespace sideshow {
enum class VideoEnd { Loop, Hold, Next };
std::optional<VideoEnd> parseVideoEnd(std::string_view value);
std::string_view videoEndName(VideoEnd value);
bool validVideoId(std::string_view value);
}
