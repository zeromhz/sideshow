#include <catch2/catch_test_macros.hpp>

#include "config.h"

using namespace sideshow;

namespace {
bool hasWarningContaining(const ConfigResult& result, const std::string& text) {
    for (const auto& w : result.warnings) {
        if (w.find(text) != std::string::npos) return true;
    }
    return false;
}
}  // namespace

TEST_CASE("Config: empty text gives all defaults and no warnings") {
    ConfigResult r = parseConfig("");
    CHECK(r.warnings.empty());
    CHECK(r.config.panelId == "panel-01");
    CHECK(r.config.display.width == 0);
    CHECK(r.config.display.height == 0);
    CHECK(r.config.display.maxWidth == 2560);
    CHECK(r.config.display.maxHeight == 1440);
    CHECK(r.config.display.fps == 60);
    CHECK(r.config.display.fullscreen);
    CHECK(r.config.audio.device.empty());
    CHECK(r.config.audio.sampleRate == 48000);
    CHECK(r.config.osc.port == 9000);
    CHECK(r.config.presets.root == "/opt/sideshow/presets");
    CHECK(r.config.presets.textures == "/opt/sideshow/textures");
    CHECK(r.config.presets.playlist == "default");
    CHECK(r.config.presets.duration == 30.0);
    CHECK(r.config.presets.softCutDuration == 3.0);
    CHECK_FALSE(r.config.presets.hardcutEnabled);
    CHECK(r.config.presets.beatSensitivity == 1.0f);
    CHECK(r.config.presets.shuffle);
    CHECK(r.config.output.masterDefault == 1.0f);
    CHECK(r.config.output.masterCurve == 2.0f);
    CHECK(r.config.output.masterSmoothingMs == 50.0f);
}

TEST_CASE("Config: values override defaults") {
    ConfigResult r = parseConfig(R"(
panel_id = "panel-07"

[display]
width = 2560
height = 1440
max_width = 1920
max_height = 1080
fullscreen = false

[audio]
device = "USB"

[osc]
port = 9100

[presets]
root = "deps/presets"
playlist = "fractal"
duration = 12
hardcut_enabled = true
beat_sensitivity = 1.5
shuffle = false

[output]
master_curve = 1.0
master_smoothing_ms = 0
)");
    CHECK(r.warnings.empty());
    CHECK(r.config.panelId == "panel-07");
    CHECK(r.config.display.width == 2560);
    CHECK(r.config.display.height == 1440);
    CHECK(r.config.display.maxWidth == 1920);
    CHECK(r.config.display.maxHeight == 1080);
    CHECK_FALSE(r.config.display.fullscreen);
    CHECK(r.config.audio.device == "USB");
    CHECK(r.config.osc.port == 9100);
    CHECK(r.config.presets.root == "deps/presets");
    CHECK(r.config.presets.playlist == "fractal");
    CHECK(r.config.presets.duration == 12.0);  // integer accepted for a float key
    CHECK(r.config.presets.hardcutEnabled);
    CHECK(r.config.presets.beatSensitivity == 1.5f);
    CHECK_FALSE(r.config.presets.shuffle);
    CHECK(r.config.output.masterCurve == 1.0f);
    CHECK(r.config.output.masterSmoothingMs == 0.0f);
}

TEST_CASE("Config: a wrong type falls back to the default for that key only") {
    ConfigResult r = parseConfig(R"(
panel_id = 5
[osc]
port = "nine thousand"
[display]
fps = 30
)");
    CHECK(r.config.panelId == "panel-01");
    CHECK(r.config.osc.port == 9000);
    CHECK(r.config.display.fps == 30);
    CHECK(hasWarningContaining(r, "panel_id"));
    CHECK(hasWarningContaining(r, "osc.port"));
}

TEST_CASE("Config: out-of-range values fall back to defaults") {
    ConfigResult r = parseConfig(R"(
panel_id = "Panel 1!"
[display]
max_width = 10
[osc]
port = 70000
[presets]
beat_sensitivity = 2.5
[output]
master_default = 1.5
)");
    CHECK(r.config.panelId == "panel-01");
    CHECK(r.config.osc.port == 9000);
    CHECK(r.config.output.masterDefault == 1.0f);
    CHECK(r.config.presets.beatSensitivity == 1.0f);
    CHECK(r.config.display.maxWidth == 2560);
    CHECK(hasWarningContaining(r, "display.max_width"));
    CHECK(hasWarningContaining(r, "panel_id"));
    CHECK(hasWarningContaining(r, "osc.port"));
    CHECK(hasWarningContaining(r, "output.master_default"));
    CHECK(hasWarningContaining(r, "presets.beat_sensitivity"));
}

TEST_CASE("Config: unparseable TOML gives defaults and a warning") {
    ConfigResult r = parseConfig("this is = = not toml");
    CHECK(r.config.osc.port == 9000);
    CHECK(hasWarningContaining(r, "parse error"));
}

TEST_CASE("Config: a missing file gives defaults and a warning") {
    ConfigResult r = loadConfigFile("/nonexistent/sideshow/config.toml");
    CHECK(r.config.panelId == "panel-01");
    CHECK(hasWarningContaining(r, "not found"));
}
