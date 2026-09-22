#pragma once

#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>

namespace sideshow {

// Line-atomic logging to stderr (journald adds timestamps on the Pi).
inline void logLine(const char* level, const std::string& message) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    std::cerr << level << message << '\n' << std::flush;
}

inline void logInfo(const std::string& message) { logLine("[info] ", message); }
inline void logWarn(const std::string& message) { logLine("[warn] ", message); }
inline void logError(const std::string& message) { logLine("[error] ", message); }

// Enabled with the environment variable SIDESHOW_DEBUG=1.
inline void logDebug(const std::string& message) {
    static const bool enabled = std::getenv("SIDESHOW_DEBUG") != nullptr;
    if (enabled) logLine("[debug] ", message);
}

}  // namespace sideshow
