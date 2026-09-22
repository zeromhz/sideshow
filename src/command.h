#pragma once

#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <variant>

namespace sideshow {

namespace cmd {
struct Next {};
struct Prev {};
struct Random {};
struct Lock { bool on; };
struct Playlist { std::string name; };
struct Preset { std::string name; };
struct Blackout { bool on; };
struct Master { float value; };
struct Duration { float seconds; };
struct BeatSensitivity { float value; };
struct HardCut { bool on; };
}  // namespace cmd

using Command = std::variant<cmd::Next, cmd::Prev, cmd::Random, cmd::Lock, cmd::Playlist,
                             cmd::Preset, cmd::Blackout, cmd::Master, cmd::Duration,
                             cmd::BeatSensitivity, cmd::HardCut>;

// Multi-producer queue drained by the main loop between frames.
class CommandQueue {
public:
    void push(Command command) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(command));
    }

    std::optional<Command> pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        Command command = std::move(queue_.front());
        queue_.pop();
        return command;
    }

private:
    std::mutex mutex_;
    std::queue<Command> queue_;
};

}  // namespace sideshow
