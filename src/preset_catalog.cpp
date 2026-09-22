#include "preset_catalog.h"

#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace sideshow {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool isPresetFile(const fs::path& p) { return lower(p.extension().string()) == ".milk"; }

std::vector<std::string> scanPlaylist(const fs::path& dir) {
    std::vector<std::string> files;
    std::error_code ec;
    const auto options = fs::directory_options::follow_directory_symlink |
                         fs::directory_options::skip_permission_denied;
    fs::recursive_directory_iterator it(dir, options, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc) && isPresetFile(it->path())) {
            files.push_back(it->path().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace

PresetCatalog::PresetCatalog(const fs::path& root) {
    std::error_code ec;
    fs::directory_iterator it(root, ec);
    for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        std::error_code dirEc;
        if (!it->is_directory(dirEc)) continue;  // follows symlinks
        playlists_[name] = scanPlaylist(it->path());
    }
    for (const auto& [playlist, files] : playlists_) {
        for (const auto& file : files) {
            byFilename_.emplace(lower(fs::path(file).filename().string()), file);
        }
    }
}

std::vector<std::string> PresetCatalog::playlists() const {
    std::vector<std::string> names;
    for (const auto& [name, files] : playlists_) names.push_back(name);
    return names;
}

bool PresetCatalog::hasPlaylist(const std::string& name) const {
    return playlists_.count(name) > 0;
}

std::vector<std::string> PresetCatalog::presetsIn(const std::string& playlist) const {
    auto found = playlists_.find(playlist);
    if (found == playlists_.end()) return {};
    std::vector<std::string> result;
    for (const auto& file : found->second) {
        if (!isBad(file)) result.push_back(file);
    }
    return result;
}

std::optional<std::string> PresetCatalog::findPreset(const std::string& name) const {
    std::string key = lower(name);
    if (!isPresetFile(key)) key += ".milk";
    auto found = byFilename_.find(key);
    if (found == byFilename_.end()) return std::nullopt;
    return found->second;
}

void PresetCatalog::markBad(const std::string& path) {
    badFilenames_.insert(lower(fs::path(path).filename().string()));
}

bool PresetCatalog::isBad(const std::string& path) const {
    return badFilenames_.count(lower(fs::path(path).filename().string())) > 0;
}

}  // namespace sideshow
