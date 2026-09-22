#pragma once

#include <projectM-4/playlist.h>
#include <projectM-4/projectM.h>

#include <optional>
#include <random>
#include <string>

#include "preset_catalog.h"

namespace sideshow {

// Drives projectM's playlist library from the PresetCatalog. Main thread only.
//
// A preset that fails to load is marked bad in the catalog (for this session), removed
// from the playlist and skipped on the next tick(). Too many failures in a row, or an
// emptied playlist, fall back to "default". Otherwise pruning continues until a preset
// loads; only an emptied playlist leaves projectM on its current (or idle) preset.
class PresetManager {
public:
    PresetManager(projectm_handle projectm, PresetCatalog& catalog, bool shuffle);
    ~PresetManager();
    PresetManager(const PresetManager&) = delete;
    PresetManager& operator=(const PresetManager&) = delete;

    // Plays `name`; falls back to "default"; if both fail, projectM's idle preset stays up.
    void start(const std::string& name);
    bool switchPlaylist(const std::string& name);  // false (no change) if missing or empty
    void next();
    void prev();
    void random();
    bool loadPreset(const std::string& name);  // false if not found, known bad, or fails to load
    void setLocked(bool locked);
    void tick();  // once per frame: skips past a preset that just failed to load

    const std::string& currentPlaylist() const { return playlistName_; }
    const std::string& currentPreset() const { return presetName_; }

private:
    void pruneBadPresets();  // removes playlist items the catalog has marked bad
    static void onSwitched(bool isHardCut, unsigned int index, void* userData);
    static void onFailed(const char* presetFilename, const char* message, void* userData);

    projectm_handle projectm_;
    projectm_playlist_handle playlist_;
    PresetCatalog& catalog_;
    std::string playlistName_;
    std::string presetName_;
    bool lastLoadFailed_ = false;             // set by onFailed, consumed by onSwitched/loadPreset
    std::optional<unsigned int> failedIndex_;  // playlist item to remove and skip in tick()
    int consecutiveFailures_ = 0;
    bool goingBack_ = false;  // prev() in progress: skip failures backwards
    std::mt19937 rng_{std::random_device{}()};
};

}  // namespace sideshow
