#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>

#include "preset_catalog.h"

namespace fs = std::filesystem;
using sideshow::PresetCatalog;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() /
               ("sideshow-test-" + std::to_string(std::random_device{}()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "[preset00]\n";
}

// root/
//   default/a.milk, default/sub/b.MILK, default/notes.txt
//   mellow/c.milk
//   linked -> mellow   (directory symlink)
//   .hidden/d.milk
//   loose.milk          (top-level file, not a playlist)
struct Fixture {
    TempDir dir;
    Fixture() {
        touch(dir.path / "default" / "a.milk");
        touch(dir.path / "default" / "sub" / "b.MILK");
        touch(dir.path / "default" / "notes.txt");
        touch(dir.path / "mellow" / "c.milk");
        touch(dir.path / ".hidden" / "d.milk");
        touch(dir.path / "loose.milk");
        fs::create_directory_symlink(dir.path / "mellow", dir.path / "linked");
    }
};

}  // namespace

TEST_CASE("PresetCatalog lists visible subdirectories as playlists, following symlinks") {
    Fixture f;
    PresetCatalog catalog(f.dir.path);
    CHECK(catalog.playlists() == std::vector<std::string>{"default", "linked", "mellow"});
    CHECK(catalog.hasPlaylist("mellow"));
    CHECK_FALSE(catalog.hasPlaylist(".hidden"));
    CHECK_FALSE(catalog.hasPlaylist("nope"));
}

TEST_CASE("PresetCatalog finds .milk files recursively, case-insensitively, sorted") {
    Fixture f;
    PresetCatalog catalog(f.dir.path);
    auto presets = catalog.presetsIn("default");
    REQUIRE(presets.size() == 2);
    CHECK(fs::path(presets[0]).filename() == "a.milk");
    CHECK(fs::path(presets[1]).filename() == "b.MILK");

    CHECK(catalog.presetsIn("linked").size() == 1);
    CHECK(catalog.presetsIn("nope").empty());
}

TEST_CASE("PresetCatalog finds presets by filename with or without extension") {
    Fixture f;
    PresetCatalog catalog(f.dir.path);
    auto withExt = catalog.findPreset("c.milk");
    REQUIRE(withExt);
    CHECK(fs::path(*withExt).filename() == "c.milk");

    auto withoutExt = catalog.findPreset("a");
    REQUIRE(withoutExt);
    CHECK(fs::path(*withoutExt).filename() == "a.milk");

    CHECK_FALSE(catalog.findPreset("zzz"));
    CHECK_FALSE(catalog.findPreset("d"));  // only in a hidden dir
}

TEST_CASE("PresetCatalog excludes presets marked bad") {
    Fixture f;
    PresetCatalog catalog(f.dir.path);
    const std::string a = catalog.presetsIn("default")[0];
    catalog.markBad(a);
    CHECK(catalog.isBad(a));
    auto presets = catalog.presetsIn("default");
    REQUIRE(presets.size() == 1);
    CHECK(fs::path(presets[0]).filename() == "b.MILK");
}

TEST_CASE("PresetCatalog tolerates a missing root") {
    PresetCatalog catalog("/nonexistent/sideshow/presets");
    CHECK(catalog.playlists().empty());
    CHECK(catalog.presetsIn("default").empty());
}
