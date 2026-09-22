#pragma once

namespace sideshow {

// Output brightness: a smoothed, curved master level with a blackout override.
class OutputLevel {
public:
    OutputLevel(float initial, float curve, float smoothingMs);

    void setTarget(float value);   // clamped to [0, 1]
    void setBlackout(bool on);
    void update(float dtSeconds);  // moves the smoothed value toward the target

    float target() const { return target_; }
    float smoothed() const { return smoothed_; }
    bool blackout() const { return blackout_; }
    float brightness() const;      // smoothed^curve, or 0 during blackout

private:
    float target_;
    float smoothed_;
    float curve_;
    float tauSeconds_;
    bool blackout_ = false;
};

}  // namespace sideshow
