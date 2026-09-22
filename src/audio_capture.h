#pragma once

#include <SDL.h>

#include <cstdint>
#include <optional>
#include <string>

#include "audio_ring.h"

namespace sideshow {

// Captures stereo float audio from an SDL capture device into an AudioRing.
// Never fails hard: if no device is available it retries every few seconds.
class AudioCapture {
public:
    AudioCapture(std::string deviceMatch, int sampleRate, AudioRing& ring);
    ~AudioCapture();
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    void tick(uint32_t nowMs);  // opens the device when closed and a retry is due
    void handleDeviceRemoved(SDL_AudioDeviceID id);
    bool isOpen() const { return device_ != 0; }

private:
    static constexpr uint32_t kRetryMs = 3000;
    static void callback(void* userdata, Uint8* stream, int len);
    std::optional<std::string> chooseDevice() const;
    void open();
    void close();

    std::string match_;
    int sampleRate_;
    AudioRing& ring_;
    SDL_AudioDeviceID device_ = 0;
    uint32_t nextRetryMs_ = 0;
    bool warned_ = false;
};

}  // namespace sideshow
