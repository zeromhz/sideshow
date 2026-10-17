#pragma once

#include "visual_mode.h"
#include "video_types.h"
#include "isf_types.h"
#include <chrono>
#include <functional>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <variant>

namespace sideshow {

struct CommandResult { std::string status; std::string detail; };

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
    using Reply = std::function<void(const CommandResult&)>;
    void push(Command command, Reply reply = {}) {
        Reply displaced;
        bool full = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Entry entry{std::move(command),std::move(reply)};
            auto* slot = std::holds_alternative<cmd::Blackout>(entry.command) ? &blackout_ :
                         std::holds_alternative<cmd::Master>(entry.command) ? &master_ : nullptr;
            if (slot) {
                if (*slot) displaced=std::move((*slot)->reply);
                *slot=std::move(entry);
            } else if (queue_.size() >= kCapacity) {
                displaced=std::move(entry.reply);full=true;
            } else queue_.push(std::move(entry));
        }
        // Replies may perform I/O or enqueue more work; never hold the queue lock.
        if (displaced) displaced({full ? "rejected" : "superseded",
            full ? "Command queue full" : "Replaced by a newer output value"});
    }

    std::optional<Command> pop() {
        auto entry=take();
        if (!entry) return std::nullopt;
        if (entry->reply) entry->reply({"accepted","Dequeued without an application result"});
        return std::move(entry->command);
    }

    template <typename Apply>
    void processFrame(Apply&& apply) {
        processAcknowledgedFrame([&](const Command& c) {
            apply(c);
            return CommandResult{"accepted","Handled without an application result"};
        });
    }

    template <typename Apply>
    void processAcknowledgedFrame(Apply&& apply) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
        for (size_t count = 0; count < 8; ++count) {
            auto entry = take();
            if (!entry) break;
            const auto result=apply(entry->command);
            if(entry->reply) entry->reply(result);
            if (std::chrono::steady_clock::now() >= deadline) break;
        }
    }

private:
    struct Entry { Command command; Reply reply; };
    std::optional<Entry> take() {
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto* slot : {&blackout_, &master_}) {
            if (*slot) {auto entry=std::move(*slot);slot->reset();return entry;}
        }
        if(queue_.empty())return std::nullopt;
        auto entry=std::move(queue_.front());queue_.pop();return entry;
    }
    static constexpr size_t kCapacity = 128;
    std::mutex mutex_;
    std::queue<Entry> queue_;
    std::optional<Entry> blackout_, master_;

};

}  // namespace sideshow
