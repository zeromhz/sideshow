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
    CHECK(r.config.display.fps == 30);
    CHECK(r.config.display.fullscreen);
    CHECK(r.config.display.ipOverlaySeconds == 60);
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
    CHECK(r.config.presets.renderScale == 2.0 / 3.0);
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
fps = 24
)");
    CHECK(r.config.panelId == "panel-01");
    CHECK(r.config.osc.port == 9000);
    CHECK(r.config.display.fps == 24);
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

TEST_CASE("Config: frame rate is capped at 30 and render scale is bounded") {
    ConfigResult r = parseConfig(R"(
[display]
fps = 60
ip_overlay_seconds = 1000
[presets]
render_scale = 0.1
)");
    CHECK(r.config.display.ipOverlaySeconds == 60);
    CHECK(hasWarningContaining(r, "display.ip_overlay_seconds"));
    CHECK(r.config.display.fps == 30);
    CHECK(r.config.presets.renderScale == 2.0 / 3.0);
    CHECK(hasWarningContaining(r, "display.fps"));
    CHECK(hasWarningContaining(r, "presets.render_scale"));
    r = parseConfig("[presets]\nrender_scale = 1.0\n");
    CHECK(r.warnings.empty());
    CHECK(r.config.presets.renderScale == 1.0);
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

TEST_CASE("Video configuration defaults and validated selections") {
    auto defaults = parseConfig("");
    CHECK(defaults.config.video.root == "/opt/sideshow/videos");
    CHECK(defaults.config.video.preset.empty());
    CHECK(defaults.config.video.end == VideoEnd::Loop);
    auto valid = parseConfig("[visuals]\nmode='video'\n[video]\nroot='clips'\npreset='live/intro.mp4'\nend='hold'\n");
    CHECK(valid.warnings.empty());
    CHECK(valid.config.visuals.mode == VisualMode::Video);
    CHECK(valid.config.video.preset == "live/intro.mp4");
    CHECK(valid.config.video.end == VideoEnd::Hold);
    auto invalid = parseConfig("[video]\nroot=7\npreset='../escape.mp4'\nend='stop'\n");
    CHECK(invalid.warnings.size() == 3);
    CHECK(invalid.config.video.preset.empty());
    CHECK(invalid.config.video.end == VideoEnd::Loop);
}
TEST_CASE("ISF configuration validates mode library and render bounds","[isf]") {
 auto r=sideshow::parseConfig("[visuals]\nmode='isf'\n[isf]\nroot='/tmp/shaders'\npreset='nested/a.fs'\nmax_width=640\nmax_height=480\n");
 CHECK(r.warnings.empty());CHECK(r.config.visuals.mode==sideshow::VisualMode::Isf);CHECK(r.config.isf.maxWidth==640);CHECK(r.config.isf.preset=="nested/a.fs");
 auto bad=sideshow::parseConfig("[isf]\nmax_width=4097\nmax_height=0\npreset='../a.fs'\n");CHECK(bad.warnings.size()==3);CHECK(bad.config.isf.maxWidth==1280);
}

TEST_CASE("ISF per-shader render scales accept shader IDs and 0.25-1","[isf]") {
 auto r=sideshow::parseConfig("[isf.scale]\n\"fractal/dive.fs\" = 0.6\n\"plasma.fs\" = 1\n");
 CHECK(r.warnings.empty());REQUIRE(r.config.isf.scale.size()==2);CHECK(r.config.isf.scale.at("fractal/dive.fs")==0.6);
 auto bad=sideshow::parseConfig("[isf.scale]\n\"a.fs\" = 0.1\n\"../b.fs\" = 0.5\n\"c.fs\" = 'half'\n\"d.fs\" = 0.75\n");
 CHECK(bad.warnings.size()==3);REQUIRE(bad.config.isf.scale.size()==1);CHECK(bad.config.isf.scale.at("d.fs")==0.75);
 CHECK(sideshow::parseConfig("[isf]\nscale = 0.5\n").warnings.size()==1);
}

TEST_CASE("Config: fluid persistence defaults and rejects unsafe multipliers") {
    CHECK(parseConfig("").config.fluid.persistence == 1.0f);
    CHECK(parseConfig("[fluid]\npersistence = 4.0").config.fluid.persistence == 4.0f);
    for (const auto* value : {"0", "-1", "11", "nan", "inf", "'slow'"}) {
        auto r = parseConfig(std::string("[fluid]\npersistence = ") + value);
        CHECK(r.config.fluid.persistence == 1.0f);
        CHECK(hasWarningContaining(r, "fluid.persistence"));
    }
}

TEST_CASE("Config: startup lock is opt-in and requires a boolean") {
    CHECK_FALSE(parseConfig("").config.presets.locked);
    CHECK(parseConfig("[presets]\nlocked = true").config.presets.locked);
    CHECK_FALSE(parseConfig("[presets]\nlocked = false").config.presets.locked);
    for (const auto* value : {"1", "'true'"}) {
        auto r=parseConfig(std::string("[presets]\nlocked = ")+value);
        CHECK_FALSE(r.config.presets.locked);
        CHECK(hasWarningContaining(r,"presets.locked"));
    }
}
