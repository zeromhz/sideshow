#pragma once
#include "visual_backend.h"
struct PendingVideoState {
    sideshow::PreparationState state=sideshow::PreparationState::Pending;
    sideshow::VisualSelection selection{".","intro.mp4"};
    sideshow::PlaybackSettings settings;
    sideshow::VideoEnd end=sideshow::VideoEnd::Loop;
    bool paused=false,broken=false;
    int width=0,restarts=0,renders=0;
};
class PendingVideoBackend : public sideshow::VisualBackend, public sideshow::VideoControl {
public:
    std::shared_ptr<PendingVideoState> s;
    PendingVideoBackend(const sideshow::BackendInit& init,std::shared_ptr<PendingVideoState> state):s(std::move(state)) {
        if(!init.selection.preset.empty()) s->selection=init.selection;
        s->settings=init.playback;s->end=init.videoEnd;s->width=init.width;
    }
    sideshow::PreparationResult preparation() override {return {s->state,"test preparation failure"};}
    sideshow::VideoControl* videoControl() override {return this;}
    void setEnd(sideshow::VideoEnd e) override {s->end=e;}
    sideshow::VideoEnd end() const override {return s->end;}
    void setPaused(bool p) override {s->paused=p;}
    void restart() override {++s->restarts;}
    void maintenance() override {}
    void addPcm(const float*,size_t) override {}
    void render(float) override {if(s->broken) throw std::runtime_error("GPU failed");++s->renders;}
    void resize(int w,int) override {s->width=w;}
    void setSettings(const sideshow::PlaybackSettings& v) override {s->settings=v;}
    void next() override {} void prev() override {} void random() override {}
    bool selectPreset(const std::string& id) override {s->selection={"nested",id};return true;}
    bool selectPlaylist(const std::string&) override {return false;}
    sideshow::VisualSelection selection() const override {return s->selection;}
};
