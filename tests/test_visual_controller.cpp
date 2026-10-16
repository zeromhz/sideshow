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
