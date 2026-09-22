#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

#include "output_level.h"

using Catch::Matchers::WithinAbs;
using sideshow::OutputLevel;

TEST_CASE("OutputLevel starts at the initial value with the curve applied") {
    OutputLevel full(1.0f, 2.0f, 50.0f);
    CHECK_THAT(full.brightness(), WithinAbs(1.0, 1e-6));

    OutputLevel half(0.5f, 2.0f, 50.0f);
    CHECK_THAT(half.smoothed(), WithinAbs(0.5, 1e-6));
    CHECK_THAT(half.brightness(), WithinAbs(0.25, 1e-6));
}

TEST_CASE("OutputLevel clamps its target to 0..1") {
    OutputLevel level(1.0f, 1.0f, 50.0f);
    level.setTarget(1.7f);
    CHECK(level.target() == 1.0f);
    level.setTarget(-0.3f);
    CHECK(level.target() == 0.0f);
}

TEST_CASE("OutputLevel smooths toward the target with the configured time constant") {
    OutputLevel level(1.0f, 1.0f, 50.0f);
    level.setTarget(0.0f);
    level.update(0.05f);  // exactly one time constant
    CHECK_THAT(level.smoothed(), WithinAbs(std::exp(-1.0), 1e-4));
    level.update(1.0f);   // 20 time constants
    CHECK_THAT(level.smoothed(), WithinAbs(0.0, 1e-4));
}

TEST_CASE("OutputLevel with zero smoothing snaps to the target") {
    OutputLevel level(1.0f, 1.0f, 0.0f);
    level.setTarget(0.3f);
    level.update(0.001f);
    CHECK_THAT(level.smoothed(), WithinAbs(0.3, 1e-6));
}

TEST_CASE("Blackout forces zero brightness but smoothing keeps running") {
    OutputLevel level(1.0f, 1.0f, 50.0f);
    level.setBlackout(true);
    CHECK(level.brightness() == 0.0f);

    level.setTarget(0.5f);
    level.update(1.0f);
    CHECK(level.brightness() == 0.0f);

    level.setBlackout(false);
    CHECK_THAT(level.brightness(), WithinAbs(0.5, 1e-3));
}
