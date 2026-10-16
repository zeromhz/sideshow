#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "osc_parse.h"

using namespace sideshow;

namespace {
std::optional<Command> parse(const std::string& path, std::vector<OscArg> args = {}) {
    return parseOscMessage("panel-03", path, args);
}
}  // namespace

TEST_CASE("OSC: the 'all' prefix and this panel's prefix are accepted") {
    auto all = parse("/sideshow/all/next");
    REQUIRE(all);
    CHECK(std::holds_alternative<cmd::Next>(*all));

    auto mine = parse("/sideshow/panel-03/prev");
    REQUIRE(mine);
    CHECK(std::holds_alternative<cmd::Prev>(*mine));
}

TEST_CASE("OSC: other panels, other prefixes, and unknown commands are ignored") {
    CHECK_FALSE(parse("/sideshow/panel-04/next"));
    CHECK_FALSE(parse("/other/all/next"));
    CHECK_FALSE(parse("/sideshow/all/explode"));
    CHECK_FALSE(parse("/sideshow/all"));
    CHECK_FALSE(parse("/sideshow/all/next/extra"));
}

TEST_CASE("OSC: argument count must match") {
    CHECK_FALSE(parse("/sideshow/all/next", {int32_t{1}}));
    CHECK_FALSE(parse("/sideshow/all/master"));
    CHECK_FALSE(parse("/sideshow/all/master", {0.5f, 0.5f}));
}

TEST_CASE("OSC: toggles accept ints and floats") {
    auto lockInt = parse("/sideshow/all/lock", {int32_t{1}});
    REQUIRE(lockInt);
    CHECK(std::get<cmd::Lock>(*lockInt).on);

    auto blackoutFloat = parse("/sideshow/all/blackout", {0.0f});
    REQUIRE(blackoutFloat);
    CHECK_FALSE(std::get<cmd::Blackout>(*blackoutFloat).on);

    auto hardcut = parse("/sideshow/all/hardcut", {1.0f});
    REQUIRE(hardcut);
    CHECK(std::get<cmd::HardCut>(*hardcut).on);

    CHECK_FALSE(parse("/sideshow/all/lock", {std::string("yes")}));
}

TEST_CASE("OSC: master accepts floats and ints, clamped to 0..1") {
    auto half = parse("/sideshow/all/master", {0.5f});
    REQUIRE(half);
    CHECK(std::get<cmd::Master>(*half).value == 0.5f);

    auto fromInt = parse("/sideshow/all/master", {int32_t{1}});
    REQUIRE(fromInt);
    CHECK(std::get<cmd::Master>(*fromInt).value == 1.0f);

    auto tooHigh = parse("/sideshow/all/master", {1.7f});
    REQUIRE(tooHigh);
    CHECK(std::get<cmd::Master>(*tooHigh).value == 1.0f);

    auto tooLow = parse("/sideshow/all/master", {-2.0f});
    REQUIRE(tooLow);
    CHECK(std::get<cmd::Master>(*tooLow).value == 0.0f);
}

TEST_CASE("OSC: non-finite floats are rejected") {
    CHECK_FALSE(parse("/sideshow/all/master", {std::numeric_limits<float>::quiet_NaN()}));
    CHECK_FALSE(parse("/sideshow/all/duration", {std::numeric_limits<float>::infinity()}));
}

TEST_CASE("OSC: duration and beat sensitivity are clamped") {
    auto shortDuration = parse("/sideshow/all/duration", {0.0f});
    REQUIRE(shortDuration);
    CHECK(std::get<cmd::Duration>(*shortDuration).seconds == 1.0f);

    auto longDuration = parse("/sideshow/all/duration", {int32_t{99999}});
    REQUIRE(longDuration);
    CHECK(std::get<cmd::Duration>(*longDuration).seconds == 3600.0f);

    auto beat = parse("/sideshow/all/beat_sensitivity", {9.0f});
    REQUIRE(beat);
    CHECK(std::get<cmd::BeatSensitivity>(*beat).value == 2.0f);
}

TEST_CASE("OSC: playlist and preset need a non-empty string") {
    auto playlist = parse("/sideshow/all/playlist", {std::string("mellow")});
    REQUIRE(playlist);
    CHECK(std::get<cmd::Playlist>(*playlist).name == "mellow");

    auto preset = parse("/sideshow/panel-03/preset", {std::string("Geiss - Swirl")});
    REQUIRE(preset);
    CHECK(std::get<cmd::Preset>(*preset).name == "Geiss - Swirl");

    CHECK_FALSE(parse("/sideshow/all/playlist", {int32_t{3}}));
    CHECK_FALSE(parse("/sideshow/all/preset", {std::string("")}));
}

TEST_CASE("OSC accepts supported visualization modes only") {
    CHECK(parseOscMessage("panel-01", "/sideshow/all/mode", {std::string("fluid")}));
    CHECK(parseOscMessage("panel-01", "/sideshow/all/mode", {std::string("milkdrop")}));
    CHECK_FALSE(parseOscMessage("panel-01", "/sideshow/all/mode", {std::string("browser")}));
}
