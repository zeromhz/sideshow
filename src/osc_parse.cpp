#include "osc_parse.h"

#include <algorithm>
#include <cmath>

namespace sideshow {
namespace {

const std::string kPrefix = "/sideshow/";

std::optional<bool> asBool(const OscArg& arg) {
    if (const auto* i = std::get_if<int32_t>(&arg)) return *i != 0;
    if (const auto* f = std::get_if<float>(&arg)) return *f != 0.0f;
    return std::nullopt;
}

std::optional<float> asFloat(const OscArg& arg) {
    if (const auto* f = std::get_if<float>(&arg)) {
        if (!std::isfinite(*f)) return std::nullopt;
        return *f;
    }
    if (const auto* i = std::get_if<int32_t>(&arg)) return static_cast<float>(*i);
    return std::nullopt;
}

std::optional<std::string> asNonEmptyString(const OscArg& arg) {
    if (const auto* s = std::get_if<std::string>(&arg); s && !s->empty()) return *s;
    return std::nullopt;
}

}  // namespace

std::optional<Command> parseOscMessage(const std::string& panelId, const std::string& path,
                                       const std::vector<OscArg>& args) {
    if (path.compare(0, kPrefix.size(), kPrefix) != 0) return std::nullopt;
    const std::string rest = path.substr(kPrefix.size());
    const auto slash = rest.find('/');
    if (slash == std::string::npos) return std::nullopt;
    const std::string target = rest.substr(0, slash);
    const std::string name = rest.substr(slash + 1);
    if (target != "all" && target != panelId) return std::nullopt;

    if (args.empty()) {
        if (name == "next") return cmd::Next{};
        if (name == "prev") return cmd::Prev{};
        if (name == "random") return cmd::Random{};
        return std::nullopt;
    }
    if (args.size() != 1) return std::nullopt;
    const OscArg& arg = args[0];

    if (name == "mode") {
        if (auto s = asNonEmptyString(arg))
            if (auto mode = parseVisualMode(*s)) return cmd::Mode{*mode};
    } else if (name == "lock") {
        if (auto b = asBool(arg)) return cmd::Lock{*b};
    } else if (name == "blackout") {
        if (auto b = asBool(arg)) return cmd::Blackout{*b};
    } else if (name == "hardcut") {
        if (auto b = asBool(arg)) return cmd::HardCut{*b};
    } else if (name == "master") {
        if (auto f = asFloat(arg)) return cmd::Master{std::clamp(*f, 0.0f, 1.0f)};
    } else if (name == "duration") {
        if (auto f = asFloat(arg)) return cmd::Duration{std::clamp(*f, 1.0f, 3600.0f)};
    } else if (name == "beat_sensitivity") {
        if (auto f = asFloat(arg)) return cmd::BeatSensitivity{std::clamp(*f, 0.0f, 2.0f)};
    } else if (name == "playlist") {
        if (auto s = asNonEmptyString(arg)) return cmd::Playlist{*s};
    } else if (name == "preset") {
        if (auto s = asNonEmptyString(arg)) return cmd::Preset{*s};
    }
    return std::nullopt;
}

}  // namespace sideshow
