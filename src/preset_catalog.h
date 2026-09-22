#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace sideshow {

// Index of preset files under a root directory, built once at construction.
// Each visible immediate subdirectory (symlinks followed) is a playlist; its
// .milk files are found recursively. Presets that fail to load can be marked
// bad (by filename) and are then excluded from playlists for this session.
class PresetCatalog {
public:
    explicit PresetCatalog(const std::filesystem::path& root);

    std::vector<std::string> playlists() const;
    bool hasPlaylist(const std::string& name) const;
    std::vector<std::string> presetsIn(const std::string& playlist) const;
    std::optional<std::string> findPreset(const std::string& name) const;

    void markBad(const std::string& path);
    bool isBad(const std::string& path) const;

private:
    std::map<std::string, std::vector<std::string>> playlists_;
    std::map<std::string, std::string> byFilename_;
    std::set<std::string> badFilenames_;
};

}  // namespace sideshow
