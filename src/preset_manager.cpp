#include "preset_manager.h"

#include <filesystem>
#include <vector>

#include "log.h"

namespace sideshow {
namespace {
// Consecutive load failures before a non-default playlist falls back to 'default'.
constexpr int kMaxConsecutiveFailures = 10;

std::string filenameOf(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}
}  // namespace

PresetManager::PresetManager(projectm_handle projectm, PresetCatalog& catalog, bool shuffle)
    : projectm_(projectm), playlist_(projectm_playlist_create(projectm)), catalog_(catalog) {
    projectm_playlist_set_shuffle(playlist_, shuffle);
    // No internal retries: every failure reaches onFailed, and tick() does the skipping.
    projectm_playlist_set_retry_count(playlist_, 0);
    projectm_playlist_set_preset_switched_event_callback(playlist_, &PresetManager::onSwitched, this);
    projectm_playlist_set_preset_switch_failed_event_callback(playlist_, &PresetManager::onFailed, this);
    // Own automatic navigation too, so it uses the cursor corrected after pruning.
    projectm_set_preset_switch_requested_event_callback(projectm_, &PresetManager::onSwitchRequested, this);
}

PresetManager::~PresetManager() {
    projectm_set_preset_switch_requested_event_callback(projectm_, nullptr, nullptr);
    projectm_playlist_destroy(playlist_);
}

void PresetManager::start(const std::string& name) {
    if (switchPlaylist(name)) return;
    if (name != "default") {
        logWarn("playlist '" + name + "' missing or empty, falling back to 'default'");
        if (switchPlaylist("default")) return;
    }
    logError("no usable playlist under the preset root; showing projectM's idle preset");
}

bool PresetManager::switchPlaylist(const std::string& name) {
    const std::vector<std::string> files = catalog_.presetsIn(name);
    if (files.empty()) return false;
    switchRequested_.reset();

    std::vector<const char*> paths;
    paths.reserve(files.size());
    for (const auto& file : files) paths.push_back(file.c_str());

    projectm_playlist_clear(playlist_);
    projectm_playlist_add_presets(playlist_, paths.data(), static_cast<uint32_t>(paths.size()), false);
    playlistName_ = name;
    failedIndex_.reset();
    badPresetsPending_ = false;
    consecutiveFailures_ = 0;
    logInfo("playlist '" + name + "' (" + std::to_string(files.size()) + " presets)");
    lastLoadFailed_ = false;
    goingBack_ = false;
    if (projectm_playlist_get_shuffle(playlist_)) {
        projectm_playlist_play_next(playlist_, false);
    } else {
        projectm_playlist_set_position(playlist_, 0, false);  // play_next would skip item 0
    }
    return true;
}

// A navigation command also completes pending recovery in the requested direction.
void PresetManager::next() {
    switchRequested_.reset();
    lastLoadFailed_ = false;
    goingBack_ = false;
    if (failedIndex_) {
        tick();
        return;
    }
    advance(false, false);
}

void PresetManager::prev() {
    switchRequested_.reset();
    lastLoadFailed_ = false;
    goingBack_ = true;
    if (failedIndex_) {
        tick();
        return;
    }
    advance(true, false);
}

void PresetManager::advance(bool backwards, bool hardCut) {
    const uint32_t size = projectm_playlist_size(playlist_);
    if (size == 0) return;
    if (projectm_playlist_get_shuffle(playlist_)) {
        if (backwards) projectm_playlist_play_previous(playlist_, hardCut);
        else projectm_playlist_play_next(playlist_, hardCut);
        return;
    }
    const uint32_t index = backwards ? (position_ == 0 ? size - 1 : position_ - 1)
                                    : (positionRemoved_ ? position_ : position_ + 1);
    projectm_playlist_set_position(playlist_, index < size ? index : 0, hardCut);
}

void PresetManager::random() {
    switchRequested_.reset();
    if (badPresetsPending_) pruneBadPresets();
    const uint32_t size = projectm_playlist_size(playlist_);
    if (size == 0) return;
    std::uniform_int_distribution<uint32_t> pick(0, size - 1);
    lastLoadFailed_ = false;
    goingBack_ = false;
    projectm_playlist_set_position(playlist_, pick(rng_), false);
}

bool PresetManager::loadPreset(const std::string& name) {
    const auto path = catalog_.findPreset(name);
    if (!path || catalog_.isBad(*path)) return false;
    // Not via the playlist, so no switched callback follows; a failure only reaches onFailed.
    lastLoadFailed_ = false;
    projectm_load_preset_file(projectm_, path->c_str(), true);
    if (lastLoadFailed_) {
        lastLoadFailed_ = false;  // projectM keeps the current preset
        pruneBadPresets();        // don't let the playlist reach it later
        return false;
    }
    pruneBadPresets();
    switchRequested_.reset();
    failedIndex_.reset();
    consecutiveFailures_ = 0;
    goingBack_ = false;
    presetName_ = filenameOf(*path);
    logInfo("preset: " + presetName_);
    return true;
}

void PresetManager::setLocked(bool locked) {
    projectm_set_preset_locked(projectm_, locked);
    logInfo(std::string("preset lock ") + (locked ? "on" : "off"));
}

void PresetManager::tick() {
    const auto requested = switchRequested_;
    switchRequested_.reset();
    // Cleanup survives a later successful command, which cancels only the recovery.
    if (badPresetsPending_) pruneBadPresets();
    if (!failedIndex_) {
        if (requested) {
            lastLoadFailed_ = false;
            goingBack_ = false;
            advance(false, *requested);
        }
        return;
    }
    const unsigned int index = *failedIndex_;
    failedIndex_.reset();

    // Keep pruning and advancing until something loads: after a failed load projectM will
    // not request another switch by itself, so stopping early would freeze the picture.
    const uint32_t size = projectm_playlist_size(playlist_);
    const bool onDefault = playlistName_ == "default";
    if (size == 0 || (!onDefault && ++consecutiveFailures_ == kMaxConsecutiveFailures)) {
        if (!onDefault) {
            logWarn("playlist '" + playlistName_ + "' keeps failing, falling back to 'default'");
            if (switchPlaylist("default")) return;
        }
        if (size == 0) {
            logError("playlist '" + playlistName_ +
                     "' has no loadable presets left; keeping the current picture");
            return;
        }
        // No usable 'default': keep pruning this playlist instead.
    }
    lastLoadFailed_ = false;
    // Removal shifted the following item into `index`, so play that one (wrapping) when
    // going forward, or the one before it after prev(); shuffle just picks another.
    if (projectm_playlist_get_shuffle(playlist_)) {
        projectm_playlist_play_next(playlist_, requested.value_or(false));
    } else if (goingBack_) {
        projectm_playlist_set_position(playlist_, index == 0 ? size - 1 : index - 1,
                                      requested.value_or(false));
    } else {
        projectm_playlist_set_position(playlist_, index < size ? index : 0, requested.value_or(false));
    }
}

void PresetManager::pruneBadPresets() {
    badPresetsPending_ = false;
    for (uint32_t i = projectm_playlist_size(playlist_); i-- > 0;) {
        char* item = projectm_playlist_item(playlist_, i);
        if (item && catalog_.isBad(item)) {
            projectm_playlist_remove_preset(playlist_, i);
            // projectM retains its raw numeric position when items are erased.
            if (i < position_) --position_;
            else if (i == position_) positionRemoved_ = true;
            // Keep a pending skip pointing at the same item (or its successor if it went).
            if (failedIndex_ && i < *failedIndex_) {
                --*failedIndex_;
            }
        }
        if (item) projectm_playlist_free_string(item);
    }
}

void PresetManager::onSwitchRequested(bool isHardCut, void* userData) {
    auto* self = static_cast<PresetManager*>(userData);
    self->switchRequested_ = isHardCut;
}

void PresetManager::onSwitched(bool /*isHardCut*/, unsigned int index, void* userData) {
    auto* self = static_cast<PresetManager*>(userData);
    self->position_ = index;
    self->positionRemoved_ = false;
    // The playlist reports the switch even when the load just failed (onFailed ran first).
    if (self->lastLoadFailed_) {
        self->lastLoadFailed_ = false;
        self->failedIndex_ = index;
        return;
    }
    self->failedIndex_.reset();  // a later switch this frame succeeded: nothing to skip
    self->consecutiveFailures_ = 0;
    self->goingBack_ = false;
    char* item = projectm_playlist_item(self->playlist_, index);
    if (item) {
        self->presetName_ = filenameOf(item);
        projectm_playlist_free_string(item);
    }
    logInfo("preset: " + self->presetName_);
}

void PresetManager::onFailed(const char* presetFilename, const char* message, void* userData) {
    // Must not switch presets here (projectM docs: may recurse); tick() skips it later.
    auto* self = static_cast<PresetManager*>(userData);
    const std::string file = presetFilename ? presetFilename : "";
    self->catalog_.markBad(file);
    self->badPresetsPending_ = true;
    self->lastLoadFailed_ = true;
    logWarn("preset failed to load, skipping it for this session: " + filenameOf(file) + " (" +
            (message ? message : "") + ")");
}

}  // namespace sideshow
