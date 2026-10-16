#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include "preset_manager.h"
#include "fake_projectm.h"

namespace fs = std::filesystem;
namespace {
struct Fixture {
    fs::path root = fs::temp_directory_path() / ("sideshow-manager-" + std::to_string(std::random_device{}()));
    projectm renderer;
    Fixture() {
        fs::create_directories(root / "default");
        for (const auto* name : {"a.milk", "bad.milk", "c.milk", "d.milk"})
            std::ofstream(root / "default" / name) << "fixture";
    }
    ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
};
}

// Catches stale automatic recovery replacing an explicit successful selection.
TEST_CASE("An explicit preset selection survives pending failure recovery") {
    Fixture f;
    sideshow::PresetCatalog catalog(f.root);
    sideshow::PresetManager manager(&f.renderer, catalog, false);
    manager.start("default");
    manager.next();
    REQUIRE(manager.loadPreset("d"));
    manager.tick();
    CHECK(manager.currentPreset() == "d.milk");
}

// Catches cleanup being discarded when a later command succeeds before tick.
TEST_CASE("A successful next command still retires earlier failed presets") {
    Fixture f;
    sideshow::PresetCatalog catalog(f.root);
    sideshow::PresetManager manager(&f.renderer, catalog, false);
    manager.start("default");
    manager.next(); // bad
    manager.next(); // c
    manager.tick();
    REQUIRE(manager.currentPreset() == "c.milk");
    manager.next();
    REQUIRE(manager.currentPreset() == "d.milk");
    manager.next();
    REQUIRE(manager.currentPreset() == "a.milk");
    manager.next();
    CHECK(manager.currentPreset() == "c.milk");
}

TEST_CASE("Previous recovers backwards and can reverse a pending next failure") {
    Fixture f;
    sideshow::PresetCatalog catalog(f.root);
    sideshow::PresetManager manager(&f.renderer, catalog, false);
    manager.start("default");
    SECTION("previous after failed next") {
        manager.next();
        manager.prev();
    }
    SECTION("failed previous recovers on tick") {
        manager.prev(); // d
        manager.prev(); // c
        manager.prev(); // bad
        manager.tick();
    }
    CHECK(manager.currentPreset() == "a.milk");
    manager.prev();
    CHECK(manager.currentPreset() == "d.milk");
}

TEST_CASE("Recovery removes every bad preset encountered before the frame ends") {
    Fixture f;
    std::ofstream(f.root / "default" / "bad2.milk") << "fixture";
    sideshow::PresetCatalog catalog(f.root);
    sideshow::PresetManager manager(&f.renderer, catalog, false);
    manager.start("default");
    manager.next(); // bad
    REQUIRE_FALSE(manager.loadPreset("bad2")); // removes both failed items
    manager.tick();
    REQUIRE(manager.currentPreset() == "c.milk");
    manager.next();
    REQUIRE(manager.currentPreset() == "d.milk");
    manager.next();
    REQUIRE(manager.currentPreset() == "a.milk");
    manager.next();
    CHECK(manager.currentPreset() == "c.milk");
}

TEST_CASE("Recovery wraps past a failed first or last preset") {
    Fixture f;
    fs::remove_all(f.root / "default");
    fs::create_directories(f.root / "default");
    SECTION("failed last preset advances to first") {
        for (const auto* name : {"a.milk", "zbad.milk"})
            std::ofstream(f.root / "default" / name) << "fixture";
        sideshow::PresetCatalog catalog(f.root);
        sideshow::PresetManager manager(&f.renderer, catalog, false);
        manager.start("default");
        manager.next();
        manager.tick();
        CHECK(manager.currentPreset() == "a.milk");
    }
    SECTION("failed first preset recovers backwards to last") {
        for (const auto* name : {"bad.milk", "c.milk", "d.milk"})
            std::ofstream(f.root / "default" / name) << "fixture";
        sideshow::PresetCatalog catalog(f.root);
        sideshow::PresetManager manager(&f.renderer, catalog, false);
        manager.start("default");
        manager.prev();
        CHECK(manager.currentPreset() == "d.milk");
    }
}

TEST_CASE("An exhausted playlist falls back to default") {
    Fixture f;
    fs::create_directories(f.root / "broken");
    std::ofstream(f.root / "broken" / "bad.milk") << "fixture";
    sideshow::PresetCatalog catalog(f.root);
    sideshow::PresetManager manager(&f.renderer, catalog, false);
    manager.start("broken");
    manager.tick();
    CHECK(manager.currentPlaylist() == "default");
    CHECK(manager.currentPreset() == "a.milk");
}

TEST_CASE("Exhausting default keeps the last successful picture without retrying") {
    Fixture f;
    fs::remove(f.root / "default" / "a.milk");
    fs::remove(f.root / "default" / "c.milk");
    fs::remove(f.root / "default" / "d.milk");
    sideshow::PresetCatalog catalog(f.root);
    sideshow::PresetManager manager(&f.renderer, catalog, false);
    manager.start("default");
    manager.tick();
    manager.tick();
    manager.next();
    CHECK(manager.currentPreset().empty());
    CHECK(f.renderer.attempts.size() == 1);
}

TEST_CASE("Repeated frame recovery advances until a usable preset loads") {
    Fixture f;
    std::ofstream(f.root / "default" / "bad2.milk") << "fixture";
    sideshow::PresetCatalog catalog(f.root);
    sideshow::PresetManager manager(&f.renderer, catalog, false);
    manager.start("default");
    manager.next();
    manager.tick(); // bad2 also fails
    manager.tick();
    CHECK(manager.currentPreset() == "c.milk");
    manager.next();
    CHECK(manager.currentPreset() == "d.milk");
}

TEST_CASE("Explicit selection survives several failures and rejects known bad presets") {
    Fixture f;
    std::ofstream(f.root / "default" / "bad2.milk") << "fixture";
    sideshow::PresetCatalog catalog(f.root);
    sideshow::PresetManager manager(&f.renderer, catalog, false);
    manager.start("default");
    manager.next();
    REQUIRE_FALSE(manager.loadPreset("bad2"));
    REQUIRE(manager.loadPreset("d"));
    manager.tick();
    CHECK(manager.currentPreset() == "d.milk");
    REQUIRE_FALSE(manager.loadPreset("bad"));
    REQUIRE_FALSE(manager.loadPreset("bad2"));
    CHECK(manager.currentPreset() == "d.milk");
}

TEST_CASE("Repeated failures fall back before exhausting a large broken playlist") {
    Fixture f;
    fs::create_directories(f.root / "broken");
    for (int i = 0; i < 12; ++i)
        std::ofstream(f.root / "broken" / ("bad" + std::to_string(i) + ".milk")) << "fixture";
    sideshow::PresetCatalog catalog(f.root);
    sideshow::PresetManager manager(&f.renderer, catalog, false);
    manager.start("broken");
    for (int i = 0; i < 10; ++i) manager.tick();
    CHECK(manager.currentPlaylist() == "default");
    CHECK(manager.currentPreset() == "a.milk");
}

TEST_CASE("One recovery prunes multiple failures delivered by the playlist") {
    Fixture f;
    std::ofstream(f.root / "default" / "bad2.milk") << "fixture";
    sideshow::PresetCatalog catalog(f.root);
    sideshow::PresetManager manager(&f.renderer, catalog, false);
    manager.start("default");
    // Model notifications originating in projectM, independently of user commands.
    projectm_playlist_play_next(f.renderer.playlist, false);
    projectm_playlist_play_next(f.renderer.playlist, false);
    manager.tick();
    REQUIRE(manager.currentPreset() == "c.milk");
    manager.next();
    REQUIRE(manager.currentPreset() == "d.milk");
    manager.next();
    REQUIRE(manager.currentPreset() == "a.milk");
    manager.next();
    CHECK(manager.currentPreset() == "c.milk");
}

TEST_CASE("Random selection does not retry a preset already known to have failed") {
    Fixture f;
    for (const auto* name : {"a.milk", "c.milk", "d.milk"})
        fs::remove(f.root / "default" / name);
    sideshow::PresetCatalog catalog(f.root);
    sideshow::PresetManager manager(&f.renderer, catalog, false);
    manager.start("default"); // the only preset fails
    manager.random();
    manager.tick();
    CHECK(f.renderer.attempts.size() == 1);
}

TEST_CASE("Direct selection preserves the playlist continuation when cleanup removes its cursor") {
    Fixture f;
    sideshow::PresetCatalog catalog(f.root);
    sideshow::PresetManager manager(&f.renderer, catalog, false);
    manager.start("default");
    manager.next(); // failed cursor at bad
    REQUIRE(manager.loadPreset("d"));
    SECTION("next visits the removed cursor's successor") {
        manager.next();
        CHECK(manager.currentPreset() == "c.milk");
    }
    SECTION("previous visits the removed cursor's predecessor") {
        manager.prev();
        CHECK(manager.currentPreset() == "a.milk");
    }
}

TEST_CASE("Timed advancement is deferred and uses the corrected playlist cursor") {
    Fixture f;
    sideshow::PresetCatalog catalog(f.root);
    sideshow::PresetManager manager(&f.renderer, catalog, false);
    manager.start("default");
    manager.next();
    REQUIRE(manager.loadPreset("d"));
    const auto attemptsBeforeRequest = f.renderer.attempts.size();
    f.renderer.requestSwitch(true);
    REQUIRE(f.renderer.attempts.size() == attemptsBeforeRequest);
    REQUIRE(manager.currentPreset() == "d.milk");
    manager.tick();
    CHECK(manager.currentPreset() == "c.milk");
    CHECK(f.renderer.lastHardCut);
}

TEST_CASE("A user selection supersedes a pending automatic switch") {
    Fixture f;
    fs::remove(f.root / "default" / "bad.milk");
    sideshow::PresetCatalog catalog(f.root);
    sideshow::PresetManager manager(&f.renderer, catalog, false);
    manager.start("default");
    f.renderer.requestSwitch(false);
    SECTION("direct selection") { REQUIRE(manager.loadPreset("d")); }
    SECTION("next") { manager.next(); }
    SECTION("previous") { manager.prev(); }
    SECTION("random") { manager.random(); }
    const auto selected = manager.currentPreset();
    const auto attempts = f.renderer.attempts.size();
    manager.tick();
    CHECK(manager.currentPreset() == selected);
    CHECK(f.renderer.attempts.size() == attempts);
}

TEST_CASE("Direct failed loads preserve the active playlist cursor") {
    Fixture f;
    sideshow::PresetCatalog catalog(f.root);
    sideshow::PresetManager manager(&f.renderer, catalog, false);
    manager.start("default");
    manager.prev(); // d, with bad earlier in the playlist
    REQUIRE_FALSE(manager.loadPreset("bad"));
    SECTION("next wraps to a") {
        manager.next();
        CHECK(manager.currentPreset() == "a.milk");
    }
    SECTION("previous reaches c") {
        manager.prev();
        CHECK(manager.currentPreset() == "c.milk");
    }
    SECTION("timed next wraps to a") {
        f.renderer.requestSwitch(false);
        manager.tick();
        CHECK(manager.currentPreset() == "a.milk");
        CHECK_FALSE(f.renderer.lastHardCut);
    }
}

TEST_CASE("Destroying the preset manager disconnects automatic switch requests") {
    Fixture f;
    sideshow::PresetCatalog catalog(f.root);
    {
        sideshow::PresetManager manager(&f.renderer, catalog, false);
        manager.start("default");
    }
    const auto attempts = f.renderer.attempts.size();
    f.renderer.requestSwitch(true);
    CHECK(f.renderer.attempts.size() == attempts);
}
