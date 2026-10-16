#pragma once
#include "visual_backend.h"
#include "video_worker.h"
#include "video_presenter.h"
#include "video_playback.h"
#include <random>
namespace sideshow {
class VideoBackend final : public VisualBackend, public VideoControl {
public:
    VideoBackend(const BackendInit&,VideoWorker&);
    ~VideoBackend();
    PreparationResult preparation() override {return preparation_;}
    VideoControl* videoControl() override {return this;}
    void maintenance() override;
    void addPcm(const float*,size_t) override {}
    void render(float dt) override;
    void resize(int w,int h) override {presenter_.resize(w,h);}
    void setSettings(const PlaybackSettings& s) override {clock_.setLocked(s.locked);}
    void next() override;void prev() override;void random() override;
    bool selectPreset(const std::string&) override;
    bool selectPlaylist(const std::string&) override;
    VisualSelection selection() const override {return selection_;}
    void setEnd(VideoEnd value) override {clock_.setEnd(value);}
    VideoEnd end() const override {return clock_.end();}
    void setPaused(bool value) override;
    void restart() override;
private:
    void request(std::vector<std::string> ids);
    void tryNext();
    void navigate(int direction,bool randomize=false);
    void failed(const std::string& error);
    VideoWorker& worker_;
    VideoPresenter presenter_;
    VideoPlayback clock_;
    uint64_t session_,generation_=0,activeGeneration_=0;
    bool loading_=false;
    PreparationResult preparation_{PreparationState::Pending,{}};
    VisualSelection selection_;
    std::shared_ptr<const VideoCatalog> catalog_;
    std::vector<std::string> attempts_;
    size_t attempt_=0;
    std::optional<std::string> deferredPreset_,deferredFolder_;
    std::string startupPreset_;
    std::mt19937 rng_{std::random_device{}()};
};
}
