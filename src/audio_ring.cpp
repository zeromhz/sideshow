#include "audio_ring.h"

#include <algorithm>

namespace sideshow {

AudioRing::AudioRing(size_t capacity) : buffer_(capacity) {}

void AudioRing::write(const float* data, size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t capacity = buffer_.size();
    if (capacity == 0) return;
    if (count >= capacity) {
        std::copy(data + (count - capacity), data + count, buffer_.begin());
        head_ = 0;
        size_ = capacity;
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        buffer_[(head_ + size_) % capacity] = data[i];
        if (size_ < capacity) {
            ++size_;
        } else {
            head_ = (head_ + 1) % capacity;
        }
    }
}

size_t AudioRing::read(float* out, size_t maxCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t n = std::min(maxCount, size_);
    for (size_t i = 0; i < n; ++i) {
        out[i] = buffer_[(head_ + i) % buffer_.size()];
    }
    if (n > 0) {
        head_ = (head_ + n) % buffer_.size();
        size_ -= n;
    }
    return n;
}

size_t AudioRing::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}

}  // namespace sideshow
