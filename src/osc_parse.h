#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "command.h"

namespace sideshow {

using OscArg = std::variant<int32_t, float, std::string>;

inline constexpr const char* kPingAddress = "/sideshow/ping";
inline constexpr const char* kPongAddress = "/sideshow/pong";

// Maps "/sideshow/{all|<panelId>}/<command>" plus arguments to a Command.
// Returns nullopt for anything not addressed to this panel or not valid.
std::optional<Command> parseOscMessage(const std::string& panelId, const std::string& path,
                                       const std::vector<OscArg>& args);

}  // namespace sideshow
