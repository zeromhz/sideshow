#include <catch2/catch_test_macros.hpp>

#include "preset_performance.h"

using namespace sideshow;

TEST_CASE("Preset performance summarizes a preset when it changes") {
    PresetPerformance perf(30);
    CHECK_FALSE(perf.update("a.milk"));
    for (int i = 0; i < 90; ++i) perf.frame(1.0 / 30);   // 3 s on time
    for (int i = 0; i < 10; ++i) perf.frame(0.1);        // 1 s of late frames
    CHECK_FALSE(perf.update("a.milk"));
    auto line = perf.update("b.milk");
    REQUIRE(line);
    CHECK(*line == "perf: a.milk 25.0 fps, 10% late, slowest 100 ms over 4 s");
}

TEST_CASE("Preset performance reports long-running presets periodically and drops short windows") {
    PresetPerformance perf(30, 10);
    perf.update("isf/fractal/dive.fs");
    for (int i = 0; i < 301; ++i) perf.frame(1.0 / 30);  // just past 10 s
    auto line = perf.update("isf/fractal/dive.fs");
    REQUIRE(line);
    CHECK(*line == "perf: isf/fractal/dive.fs 30.0 fps, 0% late, slowest 33 ms over 10 s");
    CHECK_FALSE(perf.update("isf/fractal/dive.fs"));  // window restarted
    for (int i = 0; i < 30; ++i) perf.frame(1.0 / 30);  // only 1 s before switching
    CHECK_FALSE(perf.update("fluid/lava"));
    perf.frame(-1);  // ignored
    for (int i = 0; i < 60; ++i) perf.frame(1.0 / 30);
    CHECK(perf.update("fluid/aurora"));
}
