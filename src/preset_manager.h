#pragma once

#include <projectM-4/playlist.h>
#include <projectM-4/projectM.h>

#include <random>
#include <string>

#include "preset_catalog.h"

namespace sideshow {

// Drives projectM's playlist library from the PresetCatalog. Main thread only.
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
    bool loadPreset(const std::string& name);  // false if not found in the catalog
    void setLocked(bool locked);

    const std::string& currentPlaylist() const { return playlistName_; }
    const std::string& currentPreset() const { return presetName_; }

private:
    static void onSwitched(bool isHardCut, unsigned int index, void* userData);
    static void onFailed(const char* presetFilename, const char* message, void* userData);

    projectm_handle projectm_;
    projectm_playlist_handle playlist_;
    PresetCatalog& catalog_;
    std::string playlistName_;
    std::string presetName_;
    std::mt19937 rng_{std::random_device{}()};
};

}  // namespace sideshow
