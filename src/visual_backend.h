#pragma once
#include "config.h"
#include "video_control.h"
#include "isf_control.h"
#include <functional>
#include <memory>
namespace sideshow {
struct PlaybackSettings {
    double duration = 30.0;
    bool shuffle = true, locked = false, hardcut = false;
    float beatSensitivity = 1.0f;
};
struct VisualSelection { std::string playlist, preset; };
struct BackendInit { Config config; PlaybackSettings playback; VisualSelection selection; int width, height; VideoEnd videoEnd=VideoEnd::Loop; };
enum class PreparationState { Pending, Ready, Failed };
struct PreparationResult { PreparationState state; std::string error; };
class VisualBackend {
public:
    virtual ~VisualBackend() = default;
    virtual PreparationResult preparation() { return {PreparationState::Ready,{}}; }
    virtual IsfControl* isfControl() { return nullptr; }
    virtual void clearAudioHistory() {}
    virtual VideoControl* videoControl() { return nullptr; }
    virtual void maintenance() = 0;
    virtual void addPcm(const float*, size_t) = 0;
    virtual void render(float) = 0;
    virtual void resize(int, int) = 0;
    virtual void setSettings(const PlaybackSettings&) = 0;
    virtual void next() = 0;
    virtual void prev() = 0;
    virtual void random() = 0;
    virtual bool selectPreset(const std::string&) = 0;
    virtual bool selectPlaylist(const std::string&) = 0;
    virtual VisualSelection selection() const = 0;
};
using BackendFactory = std::function<std::unique_ptr<VisualBackend>(VisualMode,const BackendInit&)>;
}
