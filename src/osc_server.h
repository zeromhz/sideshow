#pragma once

#include <lo/lo.h>

#include <mutex>
#include <string>

#include "command.h"

namespace sideshow {

// Snapshot of player state that the OSC thread reports in /sideshow/pong.
class PlayerStatus {
public:
    void set(const std::string& preset, float fps) {
        std::lock_guard<std::mutex> lock(mutex_);
        preset_ = preset;
        fps_ = fps;
    }
    void get(std::string& preset, float& fps) const {
        std::lock_guard<std::mutex> lock(mutex_);
        preset = preset_;
        fps = fps_;
    }

private:
    mutable std::mutex mutex_;
    std::string preset_;
    float fps_ = 0.0f;
};

// liblo UDP server on its own thread. Parses messages into Commands for the main loop
// and answers /sideshow/ping directly. If the port is unavailable it logs and stays idle.
class OscServer {
public:
    OscServer(int port, std::string panelId, CommandQueue& queue, const PlayerStatus& status);
    ~OscServer();
    int port() const;
    OscServer(const OscServer&) = delete;
    OscServer& operator=(const OscServer&) = delete;

private:
    static int handle(const char* path, const char* types, lo_arg** argv, int argc,
                      lo_message message, void* userData);
    static void onError(int number, const char* message, const char* where);
    void replyPong(lo_message message);
    void handleRequest(const char* types, lo_arg** argv, int argc, lo_message message);

    lo_server_thread server_ = nullptr;
    std::string panelId_;
    CommandQueue& queue_;
    const PlayerStatus& status_;
};

}  // namespace sideshow
