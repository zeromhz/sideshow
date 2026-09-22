#include "preset_manager.h"

#include <filesystem>
#include <vector>

#include "log.h"

namespace sideshow {
namespace {
std::string filenameOf(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}
}  // namespace

PresetManager::PresetManager(projectm_handle projectm, PresetCatalog& catalog, bool shuffle)
    : projectm_(projectm), playlist_(projectm_playlist_create(projectm)), catalog_(catalog) {
    projectm_playlist_set_shuffle(playlist_, shuffle);
    projectm_playlist_set_preset_switched_event_callback(playlist_, &PresetManager::onSwitched, this);
    projectm_playlist_set_preset_switch_failed_event_callback(playlist_, &PresetManager::onFailed, this);
}

PresetManager::~PresetManager() { projectm_playlist_destroy(playlist_); }

void PresetManager::start(const std::string& name) {
    if (switchPlaylist(name)) return;
    logWarn("playlist '" + name + "' missing or empty, falling back to 'default'");
    if (name != "default" && switchPlaylist("default")) return;
    logError("no usable playlist under the preset root; showing projectM's idle preset");
}

bool PresetManager::switchPlaylist(const std::string& name) {
    const std::vector<std::string> files = catalog_.presetsIn(name);
    if (files.empty()) return false;

    std::vector<const char*> paths;
    paths.reserve(files.size());
    for (const auto& file : files) paths.push_back(file.c_str());

    projectm_playlist_clear(playlist_);
    projectm_playlist_add_presets(playlist_, paths.data(), static_cast<uint32_t>(paths.size()), false);
    playlistName_ = name;
    logInfo("playlist '" + name + "' (" + std::to_string(files.size()) + " presets)");
    projectm_playlist_play_next(playlist_, false);
    return true;
}

void PresetManager::next() { projectm_playlist_play_next(playlist_, false); }

void PresetManager::prev() { projectm_playlist_play_previous(playlist_, false); }

void PresetManager::random() {
    const uint32_t size = projectm_playlist_size(playlist_);
    if (size == 0) return;
    std::uniform_int_distribution<uint32_t> pick(0, size - 1);
    projectm_playlist_set_position(playlist_, pick(rng_), false);
}

bool PresetManager::loadPreset(const std::string& name) {
    const auto path = catalog_.findPreset(name);
    if (!path) return false;
    projectm_load_preset_file(projectm_, path->c_str(), true);
    presetName_ = filenameOf(*path);
    logInfo("preset: " + presetName_);
    return true;
}

void PresetManager::setLocked(bool locked) {
    projectm_set_preset_locked(projectm_, locked);
    logInfo(std::string("preset lock ") + (locked ? "on" : "off"));
}

void PresetManager::onSwitched(bool /*isHardCut*/, unsigned int index, void* userData) {
    auto* self = static_cast<PresetManager*>(userData);
    char* item = projectm_playlist_item(self->playlist_, index);
    if (item) {
        self->presetName_ = filenameOf(item);
        projectm_playlist_free_string(item);
    }
    logInfo("preset: " + self->presetName_);
}

void PresetManager::onFailed(const char* presetFilename, const char* message, void* userData) {
    // Do not switch presets here (projectM docs: may recurse). The playlist already skipped it.
    auto* self = static_cast<PresetManager*>(userData);
    const std::string file = presetFilename ? presetFilename : "";
    self->catalog_.markBad(file);
    logWarn("preset failed, skipping for this session: " + filenameOf(file) + " (" +
            (message ? message : "") + ")");
}

}  // namespace sideshow
