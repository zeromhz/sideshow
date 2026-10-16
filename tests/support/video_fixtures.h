#pragma once
#include <filesystem>
#include <string_view>
inline std::filesystem::path videoFixture(std::string_view name) {
    return std::filesystem::path(VIDEO_FIXTURE_DIR) / name;
}
