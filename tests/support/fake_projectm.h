#pragma once
#include <projectM-4/projectM.h>
#include <string>
#include <vector>

// Deterministic external renderer: filenames containing "bad" fail synchronously.
// Keeps failed and switched callback ordering, and rejects callback-time mutations.
struct projectm {
    std::vector<std::string> attempts;
    bool inCallback = false;
    bool lastHardCut = false;
    projectm_preset_switch_requested_event requested = nullptr;
    void* requestedUser = nullptr;
    void requestSwitch(bool hardCut);
    struct projectm_playlist* playlist = nullptr;
};
