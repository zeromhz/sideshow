#pragma once
#include <optional>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>
#include <string_view>
namespace sideshow {
struct VideoFrame {
    // Keeps a worker buffer credit alive until presentation releases the frame.
    std::shared_ptr<void> bufferCredit;
    uint64_t generation = 0;
    std::string clipId;
    double pts = 0, duration = 1.0 / 30.0;
    int width = 0, height = 0, stride = 0;
    double pixelAspect = 1;
    int rotationDegrees = 0;
    std::vector<uint8_t> rgba;
};
enum class VideoEnd { Loop, Hold, Next };
std::optional<VideoEnd> parseVideoEnd(std::string_view value);
std::string_view videoEndName(VideoEnd value);
bool validVideoId(std::string_view value);
}
