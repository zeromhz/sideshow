#include "video_playback.h"
#include <cmath>
namespace sideshow {
void VideoPlayback::beginClip() { position_ = 0; eof_.reset(); issued_ = false; }
void VideoPlayback::advance(double elapsed) {
    if (!paused_ && std::isfinite(elapsed) && elapsed >= 0 && std::isfinite(position_ + elapsed))
        position_ += elapsed;
}
VideoEnd VideoPlayback::effectiveEnd() const {
    return locked_ && end_ == VideoEnd::Next ? VideoEnd::Loop : end_;
}
void VideoPlayback::setLocked(bool value) {
    const auto before = effectiveEnd(); locked_ = value;
    if (before != effectiveEnd()) issued_ = false;
}
void VideoPlayback::setEnd(VideoEnd value) {
    const auto before = effectiveEnd(); end_ = value;
    if (before != effectiveEnd()) issued_ = false;
}
void VideoPlayback::markEof(double value) {
    if (std::isfinite(value) && value >= 0) eof_ = value;
}
VideoAction VideoPlayback::takeAction() {
    if (paused_ || !eof_ || position_ < *eof_ || issued_ || effectiveEnd() == VideoEnd::Hold)
        return VideoAction::None;
    issued_ = true;
    return effectiveEnd() == VideoEnd::Loop ? VideoAction::Rewind : VideoAction::Advance;
}
VideoTimestamps::VideoTimestamps(double fps)
    : fallback_(std::isfinite(fps) && fps > 0 ? 1.0 / fps : 1.0 / 30.0) {}
void VideoTimestamps::reset() { next_ = 0; origin_.reset(); previous_.reset(); }
std::pair<double,double> VideoTimestamps::normalize(std::optional<double> pts, std::optional<double> duration) {
    double value = next_;
    if (pts && std::isfinite(*pts)) {
        if (!origin_) origin_ = *pts - next_;
        const double candidate = *pts - *origin_;
        if (std::isfinite(candidate) && candidate >= 0 && (!previous_ || candidate > *previous_))
            value = candidate;
    }
    const double length = duration && std::isfinite(*duration) && *duration > 0 ? *duration : fallback_;
    previous_ = value; next_ = value + length;
    return {value, length};
}
}
