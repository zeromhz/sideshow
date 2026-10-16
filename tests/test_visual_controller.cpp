#include <catch2/catch_test_macros.hpp>
#include "visual_controller.h"
#include "fluid_playback.h"
#include <stdexcept>
using namespace sideshow;
namespace {
// GPU boundary substitute; navigation uses the real playback implementation.
class TestBackend : public VisualBackend {
public:
    FluidPlayback playback;
    PlaybackSettings settings;
    int& live;
    bool broken = false;
    TestBackend(const BackendInit& i, int& n):playback(i.selection.preset,i.playback,7),settings(i.playback),live(n){++live;}
    ~TestBackend(){--live;}
    void maintenance() override {}
    void addPcm(const float*,size_t) override {}
    void render(float dt) override {if(broken) throw std::runtime_error("lost GPU"); playback.update(dt);}
    void resize(int,int) override {}
    void setSettings(const PlaybackSettings& s) override {settings=s;playback.setSettings(s);}
    void next() override {playback.next();} void prev() override {playback.prev();} void random() override {playback.random();}
    bool selectPreset(const std::string& n) override {return playback.select(n);}
    bool selectPlaylist(const std::string& n) override {return n=="default";}
    VisualSelection selection() const override {return {"default",std::string(playback.current().slug)};}
};
}
TEST_CASE("Mode switching preserves settings selections and working backend on failure") {
    Config c; c.visuals.mode=VisualMode::Fluid; c.presets.shuffle=false;
    int live=0; bool fail=false, null=false; TestBackend* current=nullptr;
    BackendFactory factory=[&](VisualMode, const BackendInit& init)->std::unique_ptr<VisualBackend> {
        if(fail) throw std::runtime_error("allocation");
        if(null) return nullptr;
        auto b=std::make_unique<TestBackend>(init,live); current=b.get(); return b;
    };
    {
        VisualController v(c,320,180,factory);
        v.apply(cmd::Preset{"lava"}); v.apply(cmd::Lock{true}); v.apply(cmd::Duration{1});
        fail=true; CHECK_FALSE(v.setMode(VisualMode::Milkdrop));
        CHECK(v.mode()==VisualMode::Fluid); v.render(2); CHECK(v.statusPreset()=="fluid/lava");
        fail=false; null=true; CHECK_FALSE(v.setMode(VisualMode::Milkdrop)); CHECK(live==1);
        null=false; REQUIRE(v.setMode(VisualMode::Milkdrop)); CHECK(live==1);
        REQUIRE(v.setMode(VisualMode::Fluid)); v.render(10); CHECK(v.statusPreset()=="fluid/lava");
        CHECK(current->settings.duration==1); CHECK(current->settings.locked);
        v.apply(cmd::Playlist{"missing"}); CHECK(v.statusPreset()=="fluid/lava");
        auto* before=current; REQUIRE(v.setMode(VisualMode::Fluid)); CHECK(before==current);
        current->broken=true; v.render(.01f); CHECK(v.mode()==VisualMode::Milkdrop);
    }
    CHECK(live==0);
}
TEST_CASE("Fluid startup failure falls back once and fallback failure propagates") {
    Config c; c.visuals.mode=VisualMode::Fluid; int live=0;
    BackendFactory f=[&](VisualMode m,const BackendInit& i)->std::unique_ptr<VisualBackend> {
        if(m==VisualMode::Fluid) throw std::runtime_error("unsupported");
        return std::make_unique<TestBackend>(i,live);
    };
    VisualController v(c,1,1,f); CHECK(v.mode()==VisualMode::Milkdrop);
    CHECK_THROWS(VisualController(c,1,1,[](VisualMode,const BackendInit&)->std::unique_ptr<VisualBackend>{return nullptr;}));
}

#include "support/pending_video_backend.h"
TEST_CASE("Pending video receives controls without replacing the active picture") {
    Config config;int live=0;auto state=std::make_shared<PendingVideoState>();
    BackendFactory factory=[&](VisualMode m,const BackendInit& i)->std::unique_ptr<VisualBackend> {
        if(m==VisualMode::Video) return std::make_unique<PendingVideoBackend>(i,state);
        return std::make_unique<TestBackend>(i,live);
    };
    VisualController v(config,320,180,factory);auto before=v.statusPreset();
    REQUIRE(v.setMode(VisualMode::Video));v.apply(cmd::Preset{"nested/clip.mp4"});
    v.apply(cmd::VideoEndMode{VideoEnd::Hold});v.apply(cmd::VideoPause{true});v.apply(cmd::VideoRestart{});
    v.apply(cmd::Lock{true});v.resize(640,360);v.render(.1);v.maintenance();
    CHECK(v.mode()==VisualMode::Milkdrop);CHECK(v.statusPreset()==before);
    CHECK(state->selection.preset=="nested/clip.mp4");CHECK(state->paused);CHECK(state->restarts==1);
    CHECK(state->settings.locked);CHECK(state->width==640);
    state->state=PreparationState::Ready;v.maintenance();
    CHECK(v.statusPreset()=="video/nested/clip.mp4");CHECK(live==0);
    v.setMode(VisualMode::Fluid);state=std::make_shared<PendingVideoState>();v.setMode(VisualMode::Video);
    CHECK(state->end==VideoEnd::Hold);CHECK_FALSE(state->paused);CHECK(state->selection.preset=="nested/clip.mp4");
    state->state=PreparationState::Ready;v.maintenance();state->broken=true;v.render(.1);
    CHECK(v.mode()==VisualMode::Milkdrop);
}
TEST_CASE("Superseded and failed video preparation cannot activate later") {
    Config config;config.visuals.mode=VisualMode::Video;int live=0;
    auto state=std::make_shared<PendingVideoState>();
    BackendFactory factory=[&](VisualMode m,const BackendInit& i)->std::unique_ptr<VisualBackend> {
        if(m==VisualMode::Video) return std::make_unique<PendingVideoBackend>(i,state);
        return std::make_unique<TestBackend>(i,live);
    };
    VisualController v(config,320,180,factory);CHECK(v.mode()==VisualMode::Milkdrop);
    v.setMode(VisualMode::Milkdrop);state->state=PreparationState::Ready;v.maintenance();
    CHECK(v.mode()==VisualMode::Milkdrop);
    state=std::make_shared<PendingVideoState>();v.setMode(VisualMode::Video);
    state->state=PreparationState::Failed;v.maintenance();CHECK(v.mode()==VisualMode::Milkdrop);
    v.apply(cmd::VideoPause{true});CHECK_FALSE(state->paused);
}
#include "support/pending_isf_backend.h"
TEST_CASE("ISF pending controls and audio preserve the active visual until ready","[isf]") {
 Config config;auto active=std::make_shared<PendingIsfState>(),pending=std::make_shared<PendingIsfState>();active->state=PreparationState::Ready;
 std::vector<VisualMode> created;
 VisualController controller(config,320,180,[&](VisualMode mode,const BackendInit& init){created.push_back(mode);return std::make_unique<PendingIsfBackend>(init,mode==VisualMode::Isf?pending:active,mode==VisualMode::Isf);});
 controller.apply(cmd::IsfEvent{"reset"});CHECK(active->events==0);
 REQUIRE(controller.setMode(VisualMode::Isf));controller.apply(cmd::Preset{"nested/a.fs"});controller.apply(cmd::IsfParam{"speed",{1.0f}});controller.apply(cmd::IsfEvent{"reset"});
 CHECK(controller.mode()==VisualMode::Milkdrop);CHECK(pending->parameters==1);CHECK(pending->events==1);
 float pcm[4]={};controller.addPcm(pcm,2);controller.clearAudioHistory();CHECK(active->audioFrames==2);CHECK(pending->audioFrames==2);CHECK(active->clears==1);CHECK(pending->clears==1);
 pending->state=PreparationState::Ready;controller.maintenance();CHECK(controller.mode()==VisualMode::Isf);CHECK(controller.statusPreset()=="isf/nested/a.fs");
 auto count=created.size();controller.setMode(VisualMode::Isf);CHECK(created.size()==count);
}
TEST_CASE("ISF startup prepares over MilkDrop fallback","[isf]") {
 Config c;c.visuals.mode=VisualMode::Isf;std::vector<VisualMode> created;
 VisualController v(c,320,180,[&](VisualMode m,const BackendInit& i){created.push_back(m);auto s=std::make_shared<PendingIsfState>();s->state=m==VisualMode::Isf?PreparationState::Pending:PreparationState::Ready;return std::make_unique<PendingIsfBackend>(i,s,m==VisualMode::Isf);});
 CHECK(created==std::vector<VisualMode>{VisualMode::Milkdrop,VisualMode::Isf});CHECK(v.mode()==VisualMode::Milkdrop);v.setMode(VisualMode::Milkdrop);v.maintenance();CHECK(v.mode()==VisualMode::Milkdrop);
}
