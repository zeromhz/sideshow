#pragma once
#include "visual_backend.h"
#include "fluid_playback.h"
#include "fluid_motion.h"
#include "fluid_renderer.h"
namespace sideshow {
class FluidBackend : public VisualBackend {
public:
    explicit FluidBackend(const BackendInit& init);
    void maintenance() override {}
    void addPcm(const float* pcm,size_t frames) override {motion_.addPcm(pcm,frames);}
    void render(float dt) override;
    void resize(int w,int h) override {renderer_.resize(w,h);}
    void setSettings(const PlaybackSettings& s) override {settings_=s;playback_.setSettings(s);}
    void next() override {playback_.next();}
    void prev() override {playback_.prev();}
    void random() override {playback_.random();}
    bool selectPreset(const std::string& n) override {return playback_.select(n);}
    bool selectPlaylist(const std::string& n) override {return n=="default";}
    VisualSelection selection() const override {return {"default",std::string(playback_.current().slug)};}
private:
    PlaybackSettings settings_;
    float ambientInterval_;
    FluidPlayback playback_;
    FluidMotion motion_;
    FluidRenderer renderer_;
    bool seed_=true;
};
}
