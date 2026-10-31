#include "isf_document.h"
#include "config.h"
#include "fluid_presets.h"

#include <fstream>
#include <optional>
#include <sstream>
#include <type_traits>

#include <toml++/toml.hpp>

namespace sideshow {
namespace {

std::string keyName(std::string_view section, std::string_view key) {
    if (section.empty()) return std::string(key);
    return std::string(section) + "." + std::string(key);
}

template <typename T, typename Valid>
void readKey(const toml::table& root, std::string_view section, std::string_view key, T& out,
             Valid valid, std::vector<std::string>& warnings) {
    toml::node_view<const toml::node> view = section.empty() ? root[key] : root[section][key];
    if (!view) return;  // absent: keep default

    std::optional<T> value;
    if constexpr (std::is_same_v<T, float>) {
        if (auto d = view.template value<double>()) value = static_cast<float>(*d);
    } else {
        value = view.template value<T>();
    }

    if (!value) {
        warnings.push_back(keyName(section, key) + ": wrong type, using default");
        return;
    }
    if (!valid(*value)) {
        warnings.push_back(keyName(section, key) + ": invalid value, using default");
        return;
    }
    out = *value;
}

}  // namespace

bool isValidPanelId(const std::string& id) {
    if (id.empty() || id.size() > 32 || id[0] == '-') return false;
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) return false;
    }
    return true;
}

ConfigResult parseConfig(std::string_view tomlText) {
    ConfigResult result;
    toml::table root;
    try {
        root = toml::parse(tomlText);
    } catch (const toml::parse_error& e) {
        result.warnings.push_back("config parse error: " + std::string(e.description()) +
                                  ", using all defaults");
        return result;
    }

    Config& c = result.config;
    auto& w = result.warnings;
    auto any = [](const auto&) { return true; };

    readKey(root, "", "panel_id", c.panelId, isValidPanelId, w);

    readKey(root, "display", "width", c.display.width, [](int v) { return v >= 0 && v <= 7680; }, w);
    readKey(root, "display", "height", c.display.height, [](int v) { return v >= 0 && v <= 4320; }, w);
    readKey(root, "display", "max_width", c.display.maxWidth,
            [](int v) { return v >= 320 && v <= 7680; }, w);
    readKey(root, "display", "max_height", c.display.maxHeight,
            [](int v) { return v >= 240 && v <= 4320; }, w);
    // 30 fps is the ceiling: older configs with fps = 60 warn and fall back to 30.
    readKey(root, "display", "fps", c.display.fps, [](int v) { return v >= 1 && v <= 30; }, w);
    readKey(root, "display", "fullscreen", c.display.fullscreen, any, w);
    readKey(root, "display", "ip_overlay_seconds", c.display.ipOverlaySeconds,
            [](int v) { return v >= 0 && v <= 600; }, w);

    readKey(root, "audio", "device", c.audio.device, any, w);
    readKey(root, "audio", "sample_rate", c.audio.sampleRate,
            [](int v) { return v >= 8000 && v <= 192000; }, w);

    readKey(root, "osc", "port", c.osc.port, [](int v) { return v >= 1 && v <= 65535; }, w);

    auto nonEmpty = [](const std::string& s) { return !s.empty(); };
    readKey(root, "presets", "root", c.presets.root, nonEmpty, w);
    readKey(root, "presets", "textures", c.presets.textures, nonEmpty, w);
    readKey(root, "presets", "playlist", c.presets.playlist, nonEmpty, w);
    readKey(root, "presets", "duration", c.presets.duration,
            [](double v) { return v >= 1.0 && v <= 3600.0; }, w);
    readKey(root, "presets", "soft_cut_duration", c.presets.softCutDuration,
            [](double v) { return v >= 0.0 && v <= 60.0; }, w);
    readKey(root, "presets", "hardcut_enabled", c.presets.hardcutEnabled, any, w);
    readKey(root, "presets", "beat_sensitivity", c.presets.beatSensitivity,
            [](float v) { return v >= 0.0f && v <= 2.0f; }, w);  // projectM clamps to 0..2
    readKey(root, "presets", "shuffle", c.presets.shuffle, any, w);
    readKey(root, "presets", "render_scale", c.presets.renderScale,
            [](double v) { return v >= 0.25 && v <= 1.0; }, w);
    if (auto locked = root["presets"]["locked"]; locked && !locked.is_boolean())
        w.push_back("presets.locked: expected boolean, using default");
    else
        readKey(root, "presets", "locked", c.presets.locked, any, w);

    readKey(root, "output", "master_default", c.output.masterDefault,
            [](float v) { return v >= 0.0f && v <= 1.0f; }, w);
    readKey(root, "output", "master_curve", c.output.masterCurve,
            [](float v) { return v >= 0.1f && v <= 10.0f; }, w);
    readKey(root, "output", "master_smoothing_ms", c.output.masterSmoothingMs,
            [](float v) { return v >= 0.0f && v <= 5000.0f; }, w);

    readKey(root,"isf","root",c.isf.root,nonEmpty,w);
    readKey(root,"isf","preset",c.isf.preset,[](const std::string& v){return v.empty()||validIsfId(v);},w);
    auto isfDimension=[&](const char* key,int& value){
        auto node=root["isf"][key];
        if(node&&!node.is_integer()){w.push_back(std::string("isf.")+key+": expected integer, using default");return;}
        readKey(root,"isf",key,value,[](int n){return n>0&&n<=4096;},w);
    };
    isfDimension("max_width",c.isf.maxWidth);isfDimension("max_height",c.isf.maxHeight);
    if(auto node=root["isf"]["scale"]) {
        if(const auto* table=node.as_table()) {
            for(const auto& [key,value]:*table) {
                const std::string id(key.str());
                const auto scale=value.value<double>();
                if(!validIsfId(id) || !scale || !(*scale>=0.25 && *scale<=1.0))
                    w.push_back("isf.scale.\""+id+"\": expected a shader ID and a number 0.25-1, ignoring");
                else c.isf.scale[id]=*scale;
            }
        } else w.push_back("isf.scale: expected a table of shader IDs, ignoring");
    }
    readKey(root, "video", "root", c.video.root, nonEmpty, w);
    readKey(root, "video", "preset", c.video.preset,
            [](const std::string& v) { return v.empty() || validVideoId(v); }, w);
    std::string videoEnd = "loop";
    readKey(root, "video", "end", videoEnd,
            [](const std::string& v) { return parseVideoEnd(v).has_value(); }, w);
    c.video.end = *parseVideoEnd(videoEnd);
    std::string mode = "milkdrop";
    readKey(root, "visuals", "mode", mode,
            [](const std::string& v) { return parseVisualMode(v).has_value(); }, w);
    c.visuals.mode = *parseVisualMode(mode);
    readKey(root, "fluid", "preset", c.fluid.preset,
            [](const std::string& v) { return findFluidPreset(v) != nullptr; }, w);
    readKey(root, "fluid", "persistence", c.fluid.persistence,
            [](float v) { return v >= 1.0f && v <= 10.0f; }, w);
    auto resolution = [&](const char* key, int& out, int low, int high) {
        const auto node = root["fluid"][key];
        if (node && !node.is_integer()) {
            w.push_back(std::string("fluid.") + key + ": expected integer, using default");
            return;
        }
        readKey(root, "fluid", key, out, [=](int v) { return v >= low && v <= high; }, w);
    };
    resolution("simulation_resolution", c.fluid.simulationResolution, 32, 256);
    resolution("dye_resolution", c.fluid.dyeResolution, 128, 1024);
    readKey(root, "fluid", "ambient_interval", c.fluid.ambientInterval,
            [](float v) { return v >= 0.1f && v <= 60.0f; }, w);
    return result;
}

ConfigResult loadConfigFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        ConfigResult result;
        result.warnings.push_back("config file not found: " + path + ", using defaults");
        return result;
    }
    std::stringstream text;
    text << in.rdbuf();
    return parseConfig(text.str());
}

}  // namespace sideshow
