#pragma once

#include <algorithm>
#include <vector>

#include "audio_ring.h"

namespace sideshow {

// Main-thread bridge from capture samples to projectM's retained PCM buffer.
class AudioFeed {
public:
    explicit AudioFeed(size_t maxFrames) : pcm_(maxFrames * 2) {}

    template <typename Submit>
    bool update(AudioRing& ring, bool captureActive, float dtSeconds, Submit&& submit) {
        if (pcm_.empty()) return false;
        // Drain only this frame's snapshot; a concurrent capture callback must
        // not keep the rendering thread here indefinitely.
        size_t remaining = ring.size();
        bool received = false;
        while (remaining > 0) {
            const size_t n = ring.read(pcm_.data(), std::min(remaining, pcm_.size()));
            if (n == 0) break;
            submit(pcm_.data(), n / 2);
            received = true;
            remaining -= n;
        }
        idleSeconds_ = received ? 0.0f : idleSeconds_ + std::max(0.0f, dtSeconds);
        // Normal callbacks can be 64 ms apart at the lowest configured rate.
        // Allow those gaps; clear retained projectM audio immediately on loss,
        // or after 250 ms without data even if SDL still reports an open device.
        if (!captureActive || idleSeconds_ >= 0.250f) {
            std::fill(pcm_.begin(), pcm_.end(), 0.0f);
            // Pinned projectM 4.1.7 retains 576 frames even though its public
            // submission limit is 480. Flush the whole waveform in legal chunks.
            size_t silenceFrames = 576;
            while (silenceFrames > 0) {
                const size_t frames = std::min(silenceFrames, pcm_.size() / 2);
                submit(pcm_.data(), frames);
                silenceFrames -= frames;
            }
            idleSeconds_ = 0.250f;
            return false;
        }
        return true;
    }

private:
    std::vector<float> pcm_;
    float idleSeconds_ = 0.0f;
};

}  // namespace sideshow
