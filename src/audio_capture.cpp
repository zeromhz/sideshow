#include "audio_capture.h"

#include <vector>

#include "log.h"

namespace sideshow {

AudioCapture::AudioCapture(std::string deviceMatch, int sampleRate, AudioRing& ring)
    : match_(std::move(deviceMatch)), sampleRate_(sampleRate), ring_(ring) {}

AudioCapture::~AudioCapture() { close(); }

void AudioCapture::tick(uint32_t nowMs) {
    if (device_ != 0 || nowMs < nextRetryMs_) return;
    open();
    if (device_ == 0) nextRetryMs_ = nowMs + kRetryMs;
}

void AudioCapture::handleDeviceRemoved(SDL_AudioDeviceID id) {
    if (id != device_ || device_ == 0) return;
    logWarn("audio capture device removed; rendering silence, will retry");
    close();
    nextRetryMs_ = 0;
}

namespace {
std::vector<std::string> captureDeviceNames() {
    std::vector<std::string> names;
    const int count = SDL_GetNumAudioDevices(1);  // also re-detects devices
    for (int i = 0; i < count; ++i) {
        if (const char* name = SDL_GetAudioDeviceName(i, 1)) names.emplace_back(name);
    }
    return names;
}
}  // namespace

std::optional<std::string> AudioCapture::chooseDevice(const std::vector<std::string>& names) const {
    for (const auto& name : names) {
        if (match_.empty() || name.find(match_) != std::string::npos) return name;
    }
    return std::nullopt;
}

void AudioCapture::open() {
    const std::vector<std::string> names = captureDeviceNames();
    const auto name = chooseDevice(names);
    if (!name) {
        if (!warned_) {
            std::string available;
            for (const auto& n : names) available += (available.empty() ? "'" : ", '") + n + "'";
            logWarn("no audio capture device" +
                    (match_.empty() ? std::string() : " matching '" + match_ + "'") +
                    " (available: " + (available.empty() ? "none" : available) +
                    "); rendering silence, will retry");
            warned_ = true;
        }
        return;
    }

    SDL_AudioSpec want{};
    want.freq = sampleRate_;
    want.format = AUDIO_F32SYS;
    want.channels = 2;  // SDL up-mixes a mono mic
    want.samples = 512;
    want.callback = &AudioCapture::callback;
    want.userdata = this;
    SDL_AudioSpec have{};
    device_ = SDL_OpenAudioDevice(name->c_str(), 1, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (device_ == 0) {
        if (!warned_) {
            logWarn("failed to open audio device '" + *name + "': " + SDL_GetError() + "; will retry");
            warned_ = true;
        }
        return;
    }
    warned_ = false;
    logInfo("audio capture: '" + *name + "' at " + std::to_string(have.freq) + " Hz");
    SDL_PauseAudioDevice(device_, 0);
}

void AudioCapture::close() {
    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
}

void AudioCapture::callback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<AudioCapture*>(userdata);
    self->ring_.write(reinterpret_cast<const float*>(stream), static_cast<size_t>(len) / sizeof(float));
}

}  // namespace sideshow
