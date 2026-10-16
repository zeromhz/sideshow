#include "video_catalog.h"
#include "video_types.h"
#include <algorithm>
#include <cctype>
namespace sideshow {
namespace fs = std::filesystem;
namespace {
bool eligible(const fs::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == ".mp4" || ext == ".m4v" || ext == ".mov" || ext == ".mkv" || ext == ".webm" || ext == ".avi";
}
}
VideoCatalog VideoCatalog::scan(const fs::path& root, const std::function<bool()>& cancelled) {
    VideoCatalog result;
    std::error_code ec;
    if (fs::is_symlink(fs::symlink_status(root, ec)) || ec || !fs::is_directory(root, ec) || ec) {
        result.warnings_.push_back("Video root is not an accessible directory: " + root.string());
        return result;
    }
    result.root_ = fs::canonical(root, ec);
    if (ec) { result.warnings_.push_back(ec.message()); return result; }
    // Separate directory iterators keep a failed subtree from discarding its siblings.
    std::vector<fs::path> pending{result.root_};
    bool stopped = false;
    while (!pending.empty() && !stopped) {
        if (cancelled && cancelled()) { stopped = true; break; }
        auto dir = std::move(pending.back()); pending.pop_back();
        fs::directory_iterator it(dir, ec), end;
        if (ec) { result.warnings_.push_back(dir.string() + ": " + ec.message()); ec.clear(); continue; }
        while (it != end) {
            if (cancelled && cancelled()) { stopped = true; break; }
            const auto path = it->path();
            const auto name = path.filename().string();
            auto status = it->symlink_status(ec);
            if (ec) { result.warnings_.push_back(path.string() + ": " + ec.message()); ec.clear(); }
            else if (!name.empty() && name.front() != '.' && !fs::is_symlink(status)) {
                if (fs::is_directory(status)) pending.push_back(path);
                else if (fs::is_regular_file(status) && eligible(path)) {
                    auto relative = path.lexically_relative(result.root_);
                    auto id = relative.generic_string();
                    auto folder = relative.parent_path().generic_string();
                    if (validVideoId(id)) result.clips_.push_back({id, folder.empty() ? "." : folder, path});
                }
            }
            it.increment(ec);
            if (ec) { result.warnings_.push_back(dir.string() + ": " + ec.message()); ec.clear(); break; }
        }
    }
    if (stopped) result.warnings_.push_back("Video scan cancelled");
    std::sort(result.clips_.begin(), result.clips_.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return result;
}
std::vector<std::string> VideoCatalog::inFolder(std::string_view folder) const {
    std::vector<std::string> result;
    for (const auto& clip : clips_) if (clip.folder == folder) result.push_back(clip.id);
    return result;
}
std::optional<VideoClip> VideoCatalog::find(std::string_view id) const {
    auto it = std::lower_bound(clips_.begin(), clips_.end(), id,
        [](const auto& clip, auto key) { return clip.id < key; });
    if (it != clips_.end() && it->id == id) return *it;
    return std::nullopt;
}
std::optional<fs::path> VideoCatalog::resolveForOpen(std::string_view id) const {
    if (!validVideoId(id) || !find(id) || root_.empty()) return std::nullopt;
    std::error_code ec;
    auto path = root_;
    if (fs::is_symlink(fs::symlink_status(path, ec)) || ec) return std::nullopt;
    for (const auto& component : fs::path(id)) {
        path /= component;
        auto status = fs::symlink_status(path, ec);
        if (ec || fs::is_symlink(status)) return std::nullopt;
    }
    if (!fs::is_regular_file(path, ec) || ec) return std::nullopt;
    auto resolved = fs::canonical(path, ec);
    if (ec) return std::nullopt;
    auto relative = resolved.lexically_relative(root_);
    if (relative.empty() || !validVideoId(relative.generic_string())) return std::nullopt;
    return resolved;
}
}
