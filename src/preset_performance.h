#pragma once

#include <optional>
#include <string>

namespace sideshow {

// Per-preset frame-rate summaries for the log, so slow presets can be found on the panel itself
// (e.g. to build a stage playlist): "perf: hypnotic/Foo.milk 27.3 fps, 12% late, slowest 180 ms
// over 30 s". A frame is late when it took more than 1.5 target frame times.
class PresetPerformance {
public:
    explicit PresetPerformance(double targetFps, double reportSeconds = 60);

    // Time since the previous presented frame.
    void frame(double intervalSeconds);

    // Call about once a second with the current preset ID. Returns a summary of the previous
    // preset when it changed, or of the current one every reportSeconds. Windows shorter than
    // two seconds (mode switches, rejected presets) are dropped.
    std::optional<std::string> update(const std::string& preset);

private:
    std::string summary() const;
    void reset();

    double latePeriod_, reportSeconds_;
    std::string preset_;
    double total_ = 0, slowest_ = 0;
    long frames_ = 0, late_ = 0;
};

}  // namespace sideshow
