#pragma once

#include "visual_mode.h"
#include "video_types.h"
#include "isf_types.h"
#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <variant>

namespace sideshow {

namespace cmd {
struct IsfParam {std::string name;std::vector<IsfNumber> values;};
struct IsfEvent {std::string name;};
struct VideoEndMode { VideoEnd value; };
struct VideoPause { bool on; };
struct VideoRestart {};
struct Mode { VisualMode value; };
struct Next {};
struct Prev {};
struct Random {};
struct Lock { bool on; };
struct Playlist { std::string name; };
struct Preset { std::string name; };
struct Blackout { bool on; };
struct Master { float value; };
struct Duration { float seconds; };
struct FluidPersistence { float value; };
struct BeatSensitivity { float value; };
struct HardCut { bool on; };
}  // namespace cmd

using Command = std::variant<cmd::IsfParam, cmd::IsfEvent, cmd::Mode, cmd::Next, cmd::Prev, cmd::Random, cmd::Lock, cmd::Playlist,
                             cmd::Preset, cmd::Blackout, cmd::Master, cmd::Duration,
                             cmd::FluidPersistence, cmd::BeatSensitivity, cmd::HardCut, cmd::VideoEndMode, cmd::VideoPause, cmd::VideoRestart>;

// Bounded multi-producer queue. Blackout and master each have a reserved,
// latest-value slot so navigation overload cannot discard output controls.
class CommandQueue {
public:
    void push(Command command) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto* blackout = std::get_if<cmd::Blackout>(&command)) {
            blackout_ = *blackout;
            return;
        }
        if (auto* master = std::get_if<cmd::Master>(&command)) {
            master_ = *master;
            return;
        }
        // Preserve accepted commands in order; discard new work when saturated.
        if (queue_.size() >= kCapacity) return;
        queue_.push(std::move(command));
    }

    std::optional<Command> pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (blackout_) {
            Command command = *blackout_;
            blackout_.reset();
            return command;
        }
        if (master_) {
            Command command = *master_;
            master_.reset();
            return command;
        }
        if (queue_.empty()) return std::nullopt;
        Command command = std::move(queue_.front());
        queue_.pop();
        return command;
    }

    template <typename Apply>
    void processFrame(Apply&& apply) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
        // A single preset load cannot be preempted, but no subsequent command
        // may extend an already exhausted frame budget.
        for (size_t count = 0; count < 8; ++count) {
            auto command = pop();
            if (!command) break;
            apply(*command);
            if (std::chrono::steady_clock::now() >= deadline) break;
        }
    }

private:
    static constexpr size_t kCapacity = 128;
    std::mutex mutex_;
    std::queue<Command> queue_;
    std::optional<cmd::Blackout> blackout_;
    std::optional<cmd::Master> master_;
};

}  // namespace sideshow
