#pragma once

#include <cstddef>
#include <mutex>
#include <vector>

namespace sideshow {

// Fixed-capacity float FIFO shared by the audio callback (writer) and main loop (reader).
// When full, the oldest samples are dropped. Keep writes and reads in whole stereo frames
// (even counts) with an even capacity so channel interleaving is preserved.
class AudioRing {
public:
    explicit AudioRing(size_t capacity);

    void write(const float* data, size_t count);
    size_t read(float* out, size_t maxCount);
    size_t size() const;

private:
    mutable std::mutex mutex_;
    std::vector<float> buffer_;
    size_t head_ = 0;  // index of the oldest sample
    size_t size_ = 0;
};

}  // namespace sideshow
