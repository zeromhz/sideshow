#pragma once

#include "visual_mode.h"
#include "video_types.h"
#include <string>
#include <string_view>
#include <vector>

namespace sideshow {

struct Config {
    std::string panelId = "panel-01";

    struct Display {
        int width = 0;         // 0 = automatic; >0 = explicit mode (if the screen supports it)
        int height = 0;
        int maxWidth = 2560;   // cap for automatic selection
        int maxHeight = 1440;
        int fps = 30;          // frame-rate ceiling, 1-30
        bool fullscreen = true;
        int ipOverlaySeconds = 60;  // show panel ID and IP address after boot; 0 = off
    } display;

    struct Audio {
        std::string device;  // "" = first capture device; else substring of SDL device name
        int sampleRate = 48000;
    } audio;

    struct Osc {
        int port = 9000;
    } osc;

    struct Presets {
        std::string root = "/opt/sideshow/presets";
        std::string textures = "/opt/sideshow/textures";
        std::string playlist = "default";
        double duration = 30.0;
        double softCutDuration = 3.0;
        bool hardcutEnabled = false;
        float beatSensitivity = 1.0f;
        bool shuffle = true;
        bool locked = false;
        double renderScale = 2.0 / 3.0;  // MilkDrop internal size relative to the screen
    } presets;

    struct Visuals { VisualMode mode = VisualMode::Milkdrop; } visuals;
    struct Fluid {
        std::string preset = "light-show";
        int simulationResolution = 128;
        int dyeResolution = 512;
        float ambientInterval = 2.5f;
        float persistence = 1.0f;
    } fluid;

    struct Isf {
        std::string root="/opt/sideshow/isf",preset="examples/plasma.fs";
        int maxWidth=1280,maxHeight=720;
    } isf;

    struct Video {
        std::string root = "/opt/sideshow/videos";
        std::string preset;
        VideoEnd end = VideoEnd::Loop;
    } video;

    struct Output {
        float masterDefault = 1.0f;
        float masterCurve = 2.0f;
        float masterSmoothingMs = 50.0f;
    } output;
};

struct ConfigResult {
    Config config;
    std::vector<std::string> warnings;
};

// Never fails: anything missing or invalid falls back to its default and adds a warning.
ConfigResult parseConfig(std::string_view tomlText);
ConfigResult loadConfigFile(const std::string& path);

bool isValidPanelId(const std::string& id);

}  // namespace sideshow
