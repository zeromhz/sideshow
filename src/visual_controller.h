#pragma once
#include "visual_backend.h"
#include "command.h"
#include <array>
namespace sideshow {
class VisualController {
public:
    VisualController(Config config, int width, int height, BackendFactory factory);
    bool setMode(VisualMode mode);
    CommandResult apply(const Command& command);
    void maintenance();
    void addPcm(const float* stereo,size_t frames);
    void render(float dt);
    void clearAudioHistory();
    void resize(int width,int height);
    VisualMode mode() const { return mode_; }
    std::string statusPreset() const;
private:
    BackendInit initializationFor(VisualMode mode) const;
    void recover();
    void activate(std::unique_ptr<VisualBackend>, VisualMode);
    void updateSettings();
    Config config_;
    PlaybackSettings settings_;
    int width_,height_;
    BackendFactory factory_;
    std::array<VisualSelection,kVisualModeCount> saved_;
    VisualMode mode_ = VisualMode::Milkdrop;
    std::unique_ptr<VisualBackend> active_, pending_;
    VisualMode pendingMode_=VisualMode::Milkdrop;
    VideoEnd videoEnd_=VideoEnd::Loop;
};
}
