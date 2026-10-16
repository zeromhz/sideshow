#pragma once
#include "video_types.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <utility>
namespace sideshow {
std::pair<int,int> videoOutputSize(int width, int height);
class VideoSource {
public:
    virtual ~VideoSource() = default;
    virtual std::optional<VideoFrame> next() = 0;
    virtual void rewind() = 0;
};
class VideoDecoder final : public VideoSource {
public:
    VideoDecoder(const std::filesystem::path& file, std::function<bool()> cancelled);
    ~VideoDecoder();
    std::optional<VideoFrame> next() override;
    void rewind() override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
using VideoDecoderFactory = std::function<std::unique_ptr<VideoSource>(const std::filesystem::path&, std::function<bool()>)>;
}
