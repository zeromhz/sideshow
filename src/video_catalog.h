#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace sideshow {
struct VideoClip { std::string id, folder; std::filesystem::path path; };
class VideoCatalog {
public:
    static VideoCatalog scan(const std::filesystem::path& root, const std::function<bool()>& cancelled);
    const std::vector<VideoClip>& clips() const { return clips_; }
    std::vector<std::string> inFolder(std::string_view folder) const;
    std::optional<VideoClip> find(std::string_view id) const;
    std::optional<std::filesystem::path> resolveForOpen(std::string_view id) const;
    const std::vector<std::string>& warnings() const { return warnings_; }
private:
    std::filesystem::path root_;
    std::vector<VideoClip> clips_;
    std::vector<std::string> warnings_;
};
}
