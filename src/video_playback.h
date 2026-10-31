#pragma once
#include "video_types.h"
#include <utility>
namespace sideshow {
enum class VideoAction { None, Rewind, Advance };
class VideoPlayback {
public:
    explicit VideoPlayback(VideoEnd end = VideoEnd::Loop) : end_(end) {}
    void beginClip();
    void advance(double elapsedSeconds);
    void setPaused(bool value) { paused_ = value; }
    bool paused() const { return paused_; }
    void setLocked(bool value);
    void setEnd(VideoEnd value);
    VideoEnd end() const { return end_; }
    bool loops() const { return effectiveEnd() == VideoEnd::Loop; }
    void markEof(double endSeconds);
    VideoAction takeAction();
    double position() const { return position_; }
private:
    VideoEnd effectiveEnd() const;
    VideoEnd end_;
    bool paused_ = false, locked_ = false, issued_ = false;
    double position_ = 0;
    std::optional<double> eof_;
};
class VideoTimestamps {
public:
    explicit VideoTimestamps(double fallbackFps);
    std::pair<double,double> normalize(std::optional<double> pts, std::optional<double> duration);
    void reset();
private:
    double fallback_, next_ = 0;
    std::optional<double> origin_, previous_;
};
}
