#include <catch2/catch_test_macros.hpp>
#include "fluid_playback.h"
#include "fluid_motion.h"
#include <limits>
using namespace sideshow;
TEST_CASE("Fluid navigation wraps and unknown selections preserve the look") {
    PlaybackSettings s; s.shuffle = false;
    FluidPlayback p("light-show", s, 7);
    p.prev(); CHECK(p.current().slug == "overhead");
    p.next(); CHECK(p.current().slug == "light-show");
    p.next(); CHECK(p.current().slug == "lava");
    CHECK_FALSE(p.select("missing")); CHECK(p.current().slug == "lava");
    p.update(29); p.select("ink"); p.update(2); CHECK(p.current().slug == "ink");
    p.update(100); CHECK(p.current().slug == "frost");
    s.locked = true; p.setSettings(s); p.update(100); CHECK(p.current().slug == "frost");
}
TEST_CASE("Fluid random navigation is seeded") {
    PlaybackSettings s;
    FluidPlayback a("lava",s,5), b("lava",s,5);
    for (int i=0;i<20;++i) { a.next(); b.next(); CHECK(a.current().slug == b.current().slug); }
}
TEST_CASE("Fluid motion bounds silence ambient and onset bursts") {
    FluidMotion m(7);
    const auto& p = *findFluidPreset("lava");
    CHECK_FALSE(m.seed(p).empty());
    CHECK(m.update(100,0,2.5f,p).size() == 1);
    CHECK(m.update(0,0,2.5f,p).empty());
    float loud[] = {1,1,1,1};
    m.addPcm(loud,2);
    auto burst = m.update(.01f,1,2.5f,p);
    CHECK_FALSE(burst.empty()); CHECK(burst.size() <= 3);
    m.addPcm(loud,2); CHECK(m.update(.01f,1,2.5f,p).empty());
    CHECK(m.update(.2f,1,2.5f,p).empty()); // no stale audio
    float bad[] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
    m.addPcm(bad,1); CHECK(m.update(.2f,1,2.5f,p).empty());
    m.addPcm(loud,2); CHECK(m.update(.2f,0,2.5f,p).empty());
}
