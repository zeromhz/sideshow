#include "fake_projectm.h"
#include <projectM-4/playlist.h>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

struct projectm_playlist {
    projectm* renderer;
    std::vector<std::string> items;
    uint32_t position = 0;
    bool shuffle = false;
    projectm_playlist_preset_switched_event switched = nullptr;
    projectm_playlist_preset_switch_failed_event failed = nullptr;
    void* switchedUser = nullptr;
    void* failedUser = nullptr;
};

namespace {
void requireOutsideCallback(projectm* renderer) {
    if (renderer->inCallback) throw std::logic_error("playlist mutation or recursive load in callback");
}
}
void projectm::requestSwitch(bool hardCut) {
    inCallback = true;
    if (requested) requested(hardCut, requestedUser);
    inCallback = false;
}
extern "C" {
void projectm_set_preset_switch_requested_event_callback(projectm_handle r, projectm_preset_switch_requested_event cb, void* u) {
    r->requested = cb;
    r->requestedUser = u;
}
projectm_playlist_handle projectm_playlist_create(projectm_handle r) {
    r->playlist = new projectm_playlist{r};
    r->requested = [](bool hard, void* user) {
        projectm_playlist_play_next(static_cast<projectm_playlist_handle>(user), hard);
    };
    r->requestedUser = r->playlist;
    return r->playlist;
}
void projectm_playlist_destroy(projectm_playlist_handle p) { p->renderer->playlist = nullptr; delete p; }
void projectm_playlist_set_shuffle(projectm_playlist_handle p, bool v) { p->shuffle = v; }
bool projectm_playlist_get_shuffle(projectm_playlist_handle p) { return p->shuffle; }
void projectm_playlist_set_retry_count(projectm_playlist_handle, uint32_t) {}
void projectm_playlist_set_preset_switched_event_callback(projectm_playlist_handle p, projectm_playlist_preset_switched_event cb, void* u) { p->switched = cb; p->switchedUser = u; }
void projectm_playlist_set_preset_switch_failed_event_callback(projectm_playlist_handle p, projectm_playlist_preset_switch_failed_event cb, void* u) { p->failed = cb; p->failedUser = u; }
void projectm_playlist_clear(projectm_playlist_handle p) { requireOutsideCallback(p->renderer); p->items.clear(); p->position = 0; }
uint32_t projectm_playlist_add_presets(projectm_playlist_handle p, const char** paths, uint32_t n, bool) {
    requireOutsideCallback(p->renderer);
    for (uint32_t i = 0; i < n; ++i) p->items.emplace_back(paths[i]);
    return n;
}
uint32_t projectm_playlist_size(projectm_playlist_handle p) { return static_cast<uint32_t>(p->items.size()); }
char* projectm_playlist_item(projectm_playlist_handle p, uint32_t i) { return i < p->items.size() ? strdup(p->items[i].c_str()) : nullptr; }
void projectm_playlist_free_string(char* item) { free(item); }
bool projectm_playlist_remove_preset(projectm_playlist_handle p, uint32_t i) {
    requireOutsideCallback(p->renderer);
    if (i >= p->items.size()) return false;
    p->items.erase(p->items.begin() + i);
    // projectM 4.1.7 RemoveItem leaves its numeric position unchanged.
    return true;
}
void projectm_load_preset_file(projectm_handle r, const char* path, bool hard) {
    requireOutsideCallback(r);
    r->lastHardCut = hard;
    r->attempts.emplace_back(path);
    if (std::string(path).find("bad") == std::string::npos) return;
    r->inCallback = true;
    r->playlist->failed(path, "fixture load failure", r->playlist->failedUser);
    r->inCallback = false;
}
uint32_t projectm_playlist_set_position(projectm_playlist_handle p, uint32_t i, bool hard) {
    requireOutsideCallback(p->renderer);
    if (p->items.empty()) return 0;
    p->position = i < p->items.size() ? i : 0;
    projectm_load_preset_file(p->renderer, p->items[p->position].c_str(), hard);
    p->renderer->inCallback = true;
    p->switched(hard, p->position, p->switchedUser);
    p->renderer->inCallback = false;
    return p->position;
}
uint32_t projectm_playlist_play_next(projectm_playlist_handle p, bool hard) { return projectm_playlist_set_position(p, p->position + 1, hard); }
uint32_t projectm_playlist_play_previous(projectm_playlist_handle p, bool hard) { return projectm_playlist_set_position(p, p->position == 0 ? static_cast<uint32_t>(p->items.size()) - 1 : p->position - 1, hard); }
void projectm_set_preset_locked(projectm_handle, bool) {}
}
