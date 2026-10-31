#include <catch2/catch_test_macros.hpp>

#include "display_mode.h"

using namespace sideshow;

namespace {
DisplayMode pickedMode(const std::vector<DisplayMode>& modes, const DisplayMode& native,
                       int maxWidth = 2560, int maxHeight = 1440) {
    auto index = pickAutoMode(modes, native, maxWidth, maxHeight);
    REQUIRE(index);
    return modes[*index];
}
}  // namespace

TEST_CASE("Auto mode uses the native size when it fits the cap") {
    const std::vector<DisplayMode> modes = {
        {3840, 2160, 60}, {2560, 1440, 60}, {1920, 1080, 60}, {1280, 720, 60}};
    DisplayMode m = pickedMode(modes, {2560, 1440, 60});
    CHECK(m.width == 2560);
    CHECK(m.height == 1440);

    DisplayMode small = pickedMode(modes, {1920, 1080, 60});
    CHECK(small.width == 1920);
}

TEST_CASE("Auto mode steps a 4K screen down to the largest mode within the cap") {
    const std::vector<DisplayMode> modes = {
        {3840, 2160, 60}, {2560, 1440, 60}, {1920, 1080, 60}};
    DisplayMode m = pickedMode(modes, {3840, 2160, 60});
    CHECK(m.width == 2560);
    CHECK(m.height == 1440);

    DisplayMode capped = pickedMode(modes, {3840, 2160, 60}, 1920, 1080);
    CHECK(capped.width == 1920);
}

TEST_CASE("Auto mode prefers the native aspect ratio over a larger mode") {
    // A 4K TV without 1440p: 1920x1200 is larger than 1920x1080 but 16:10.
    const std::vector<DisplayMode> modes = {
        {3840, 2160, 60}, {1920, 1200, 60}, {1920, 1080, 60}, {1280, 720, 60}};
    DisplayMode m = pickedMode(modes, {3840, 2160, 60});
    CHECK(m.width == 1920);
    CHECK(m.height == 1080);
}

TEST_CASE("Auto mode prefers 60 Hz over higher or lower refresh rates") {
    const std::vector<DisplayMode> highRefresh = {
        {2560, 1440, 144}, {2560, 1440, 120}, {2560, 1440, 60}};
    CHECK(pickedMode(highRefresh, {2560, 1440, 144}).refreshRate == 60);

    const std::vector<DisplayMode> tv = {{1920, 1080, 24}, {1920, 1080, 50}, {1920, 1080, 60}};
    CHECK(pickedMode(tv, {1920, 1080, 60}).refreshRate == 60);

    const std::vector<DisplayMode> pal = {{1920, 1080, 24}, {1920, 1080, 50}};
    CHECK(pickedMode(pal, {1920, 1080, 50}).refreshRate == 50);
}

TEST_CASE("Auto mode accepts unknown refresh rates") {
    const std::vector<DisplayMode> modes = {{1920, 1080, 0}};
    CHECK(pickedMode(modes, {1920, 1080, 0}).width == 1920);
}

TEST_CASE("Auto mode returns nothing when no mode fits the cap") {
    const std::vector<DisplayMode> modes = {{3840, 2160, 60}, {3840, 2160, 30}};
    CHECK_FALSE(pickAutoMode(modes, {3840, 2160, 60}, 2560, 1440));
}

TEST_CASE("Exact mode lookup finds the size with the best refresh rate") {
    const std::vector<DisplayMode> modes = {
        {2560, 1440, 144}, {2560, 1440, 60}, {1920, 1080, 60}};
    auto index = findExactMode(modes, 2560, 1440);
    REQUIRE(index);
    CHECK(modes[*index].refreshRate == 60);
    CHECK_FALSE(findExactMode(modes, 1234, 567));
}

TEST_CASE("scaledRenderSize keeps the aspect ratio at the requested fraction") {
    auto s = scaledRenderSize(1920, 1080, 2.0 / 3.0);
    CHECK(s.width == 1280);
    CHECK(s.height == 720);
    s = scaledRenderSize(2560, 1440, 2.0 / 3.0);
    CHECK(s.width == 1707);
    CHECK(s.height == 960);
    s = scaledRenderSize(1080, 1920, 2.0 / 3.0);  // portrait
    CHECK(s.width == 720);
    CHECK(s.height == 1280);
    s = scaledRenderSize(1920, 1200, 0.5);
    CHECK(s.width == 960);
    CHECK(s.height == 600);
    s = scaledRenderSize(1920, 1080, 1.0);
    CHECK(s.width == 1920);
    CHECK(s.height == 1080);
    s = scaledRenderSize(1, 1, 0.25);  // never empty
    CHECK(s.width == 1);
    CHECK(s.height == 1);
    s = scaledRenderSize(1280, 720, 0.0);  // invalid scale renders at full size
    CHECK(s.width == 1280);
    CHECK(s.height == 720);
}

TEST_CASE("refreshesPerFrame shows each frame for whole refreshes without undershooting") {
    CHECK(refreshesPerFrame(60, 30) == 2);
    CHECK(refreshesPerFrame(59, 30) == 2);  // 59.94 Hz reported as 59
    CHECK(refreshesPerFrame(50, 30) == 1);  // limiter caps 50 down to 30
    CHECK(refreshesPerFrame(120, 30) == 4);
    CHECK(refreshesPerFrame(60, 60) == 1);
    CHECK(refreshesPerFrame(30, 30) == 1);
    CHECK(refreshesPerFrame(24, 30) == 1);
    CHECK(refreshesPerFrame(0, 30) == 1);   // unknown refresh
}
