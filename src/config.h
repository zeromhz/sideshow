#pragma once

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
        int fps = 60;
        bool fullscreen = true;
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
    } presets;

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
