#pragma once
#include "visual_backend.h"
#include "renderer.h"
#include "preset_catalog.h"
#include "preset_manager.h"
namespace sideshow {
class MilkdropBackend : public VisualBackend {
public:
    explicit MilkdropBackend(const BackendInit& init);
    void maintenance() override { presets_.tick(); }
    void addPcm(const float* pcm,size_t frames) override { renderer_.addPcm(pcm,static_cast<unsigned int>(frames)); }
    void render(float) override {renderer_.renderFrame();}
    void resize(int w,int h) override {renderer_.resize(w,h);}
    void setSettings(const PlaybackSettings& s) override;
    void next() override {presets_.next();}
    void prev() override {presets_.prev();}
    void random() override {presets_.random();}
    bool selectPreset(const std::string& n) override {return presets_.loadPreset(n);}
    bool selectPlaylist(const std::string& n) override {return presets_.switchPlaylist(n);}
    VisualSelection selection() const override {return {presets_.currentPlaylist(),presets_.currentPreset()};}
private:
    Renderer renderer_;
    PresetCatalog catalog_;
    PresetManager presets_;
};
}
