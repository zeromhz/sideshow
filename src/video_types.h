#pragma once
#include <optional>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>
#include <string_view>
namespace sideshow {
// Rgba: `stride` bytes per row of RGBA8. Yuv420: 8-bit planar 4:2:0 in one buffer, the luma
// plane (`stride` bytes per row, `height` rows) followed by the U and V planes (`chromaStride`
// bytes per row, (height + 1) / 2 rows each, (width + 1) / 2 samples per row). The presenter
// converts YUV to RGB on the GPU with `matrix` and `fullRange`.
enum class VideoPixels { Rgba, Yuv420 };
enum class VideoMatrix { Bt601, Bt709, Bt2020 };
struct VideoFrame {
    // Keeps a worker buffer credit alive until presentation releases the frame.
    std::shared_ptr<void> bufferCredit;
    uint64_t generation = 0;
    std::string clipId;
    double pts = 0, duration = 1.0 / 30.0;
    VideoPixels format = VideoPixels::Rgba;
    int width = 0, height = 0, stride = 0, chromaStride = 0;
    VideoMatrix matrix = VideoMatrix::Bt709;
    bool fullRange = false;
    double pixelAspect = 1;
    int rotationDegrees = 0;
    std::vector<uint8_t> pixels;
};
enum class VideoEnd { Loop, Hold, Next };
std::optional<VideoEnd> parseVideoEnd(std::string_view value);
std::string_view videoEndName(VideoEnd value);
bool validVideoId(std::string_view value);
}
