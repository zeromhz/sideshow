# Sideshow Player Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Raspberry Pi 5 boots straight into fullscreen, audio-reactive projectM (MilkDrop) visuals driven by a USB mic, controllable over OSC (including a VDMX master fader).

**Architecture:** One C++17 process, `sideshow-player`. Pure logic (config, OSC parsing, output level, audio ring buffer, preset catalog) lives in a `sideshow_core` static library that is unit-tested with Catch2 on any machine. The player executable adds SDL2 (window/GL context/audio capture), libprojectM v4.1.7 + its playlist library (rendering/presets), and liblo (OSC server thread). Only the main thread touches projectM/GL; the audio and OSC threads communicate through a ring buffer and a command queue.

**Tech Stack:** C++17, CMake ≥ 3.20, libprojectM 4.1.7 (built from source), SDL2, liblo, toml++ 3.4.0 and Catch2 3.7.1 (both via FetchContent), Python 3 (stdlib only) for the OSC CLI, bash + systemd for Pi provisioning.

**Spec:** `docs/superpowers/specs/2026-09-22-sideshow-player-design.md`

## Verified facts this plan relies on

- projectM **v4.1.7** builds on macOS (desktop GL) and installs CMake packages `projectM4` (target `libprojectM::projectM`) and `projectM4Playlist` (target `libprojectM::playlist`). Clone with `--recurse-submodules` (vendored `projectm-eval`). GLES is `-DENABLE_GLES=ON` (Linux only).
- In 4.1.7, `projectm_opengl_render_frame()` always draws to framebuffer 0 (no custom-FBO API). The master fader is therefore applied **after** projectM with a multiply-blend fullscreen triangle (`glBlendFunc(GL_ZERO, GL_SRC_ALPHA)`), not via our own FBO.
- API signatures used: `projectm_pcm_add_float(h, const float*, unsigned count_per_channel, PROJECTM_STEREO)`, `projectm_pcm_get_max_samples()`, `projectm_set_texture_search_paths(h, const char**, size_t)` (searched recursively), `projectm_playlist_create(pm)` (auto-connects), `projectm_playlist_add_presets(pl, const char**, uint32_t, bool allow_duplicates)`, `projectm_playlist_set_position(pl, uint32_t, bool hard_cut)`, `projectm_playlist_play_next/previous(pl, bool hard_cut)`, `projectm_playlist_item(pl, idx)` + `projectm_playlist_free_string`, switched callback `(bool is_hard_cut, unsigned index, void*)`, failed callback `(const char* file, const char* msg, void*)` — never switch presets inside callbacks.
- Preset pack: `mickabrig7/projectM-presets-rpi5` @ `60eaf82081cd247c466829a47713eeb19dc0d1fa` (7,998 presets: `presets/creamofthecrop` in category dirs Dancer, Drawing, Fractal, Geometric, Hypnotic, Particles, Reaction, Sparkle, Supernova, Waveform, `! Transition` (7,172); `presets/isoscelesmashup` (531); `presets/BLTC` (266); `presets/EnD` (29). Pi-5-filtered: kept if it averaged ≥24 fps on the author's Pi 5 (projectMSDL benchmark). Textures: its own `textures/` (71 files) plus `projectM-visualizer/presets-milkdrop-texture-pack` @ `6368812f27bc747b517218fbf89d21d59afce4d9`, copied into a real `textures/` directory (not a symlinked checkout, since projectM's texture search doesn't follow nested directory symlinks).
- macOS dev box has Homebrew, CMake 4.x, and SDL2 (sdl2-compat) with CMake config; liblo must be installed with `brew install liblo`.

## File map

| File | Responsibility |
|---|---|
| `CMakeLists.txt` | Builds `sideshow_core`, `sideshow_tests`, and (opt-in) `sideshow-player` |
| `src/output_level.{h,cpp}` | Master fader smoothing/curve + blackout → brightness |
| `src/command.h` | `Command` variant + thread-safe `CommandQueue` |
| `src/osc_parse.{h,cpp}` | OSC address/args → `Command` (pure, no liblo) |
| `src/audio_ring.{h,cpp}` | Thread-safe float ring buffer (audio thread → main) |
| `src/config.{h,cpp}` | TOML config with per-key validation and defaults |
| `src/preset_catalog.{h,cpp}` | Index of playlists (subdirs) and `.milk` files; bad-preset set |
| `src/display_mode.{h,cpp}` | Choose the screen mode: explicit size, else native capped at max size, prefer 60 Hz |
| `src/log.h` | Thread-safe stderr logging |
| `src/gl.h` | Platform GL header selection |
| `src/display.{h,cpp}` | SDL window + GL context (KMS/DRM on Pi); applies the chosen mode |
| `src/brightness_pass.{h,cpp}` | Multiply-blend fullscreen pass for master/blackout |
| `src/renderer.{h,cpp}` | Owns projectM instance, PCM feed, render + brightness |
| `src/audio_capture.{h,cpp}` | SDL capture device selection, callback, retry |
| `src/preset_manager.{h,cpp}` | projectM playlist wiring: switch playlists, next/prev/random/load/lock |
| `src/osc_server.{h,cpp}` | liblo server thread, arg conversion, ping/pong, `PlayerStatus` |
| `src/main.cpp` | Wiring and the frame loop |
| `tests/test_*.cpp` | Catch2 unit tests for core units |
| `tools/osc_send.py`, `tools/test_osc_send.py` | Dependency-free OSC CLI + its tests |
| `setup/build_projectm.sh` | Build/install pinned libprojectM (idempotent) |
| `setup/fetch_presets.sh` | Fetch pinned preset/texture packs, create playlist symlinks |
| `setup/dev-macos.sh` | One-time Mac dev setup |
| `setup/config.dev.toml` | Windowed config using `./deps` paths |
| `setup/config.example.toml` | Production config template |
| `setup/sideshow-player.service` | systemd unit |
| `setup/install.sh` | Idempotent Pi provisioning |
| `README.md` | Setup, VDMX, OSC reference, smoke checklist |

## Build/test commands used throughout

```bash
cmake -S . -B build                         # core + tests only (no projectM/SDL needed)
cmake --build build -j
./build/sideshow_tests                      # all unit tests
```

Player build on the Mac (after Task 8):

```bash
cmake -S . -B build -DSIDESHOW_BUILD_PLAYER=ON -DCMAKE_PREFIX_PATH="$PWD/deps/install"
cmake --build build -j
./build/sideshow-player --config setup/config.dev.toml
```

---

### Task 1: Project scaffolding + OutputLevel

**Files:**
- Create: `.gitignore`, `CMakeLists.txt`, `src/output_level.h`, `src/output_level.cpp`, `tests/test_output_level.cpp`

- [ ] **Step 1: Create `.gitignore`**

```gitignore
build/
build-*/
deps/
__pycache__/
*.pyc
.DS_Store
```

- [ ] **Step 2: Create `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)
project(sideshow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SIDESHOW_BUILD_PLAYER "Build sideshow-player (needs projectM, SDL2, liblo)" OFF)
option(SIDESHOW_BUILD_TESTS "Build unit tests" ON)

include(FetchContent)

# Pure logic with no GPU/audio/network dependencies -- unit tested on any machine.
add_library(sideshow_core STATIC
    src/output_level.cpp
)
target_include_directories(sideshow_core PUBLIC src)
target_compile_options(sideshow_core PRIVATE -Wall -Wextra)

if(SIDESHOW_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    enable_testing()
    add_executable(sideshow_tests
        tests/test_output_level.cpp
    )
    target_link_libraries(sideshow_tests PRIVATE sideshow_core Catch2::Catch2WithMain)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(sideshow_tests)
endif()
```

- [ ] **Step 3: Write the failing test `tests/test_output_level.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

#include "output_level.h"

using Catch::Matchers::WithinAbs;
using sideshow::OutputLevel;

TEST_CASE("OutputLevel starts at the initial value with the curve applied") {
    OutputLevel full(1.0f, 2.0f, 50.0f);
    CHECK_THAT(full.brightness(), WithinAbs(1.0, 1e-6));

    OutputLevel half(0.5f, 2.0f, 50.0f);
    CHECK_THAT(half.smoothed(), WithinAbs(0.5, 1e-6));
    CHECK_THAT(half.brightness(), WithinAbs(0.25, 1e-6));
}

TEST_CASE("OutputLevel clamps its target to 0..1") {
    OutputLevel level(1.0f, 1.0f, 50.0f);
    level.setTarget(1.7f);
    CHECK(level.target() == 1.0f);
    level.setTarget(-0.3f);
    CHECK(level.target() == 0.0f);
}

TEST_CASE("OutputLevel smooths toward the target with the configured time constant") {
    OutputLevel level(1.0f, 1.0f, 50.0f);
    level.setTarget(0.0f);
    level.update(0.05f);  // exactly one time constant
    CHECK_THAT(level.smoothed(), WithinAbs(std::exp(-1.0), 1e-4));
    level.update(1.0f);   // 20 time constants
    CHECK_THAT(level.smoothed(), WithinAbs(0.0, 1e-4));
}

TEST_CASE("OutputLevel with zero smoothing snaps to the target") {
    OutputLevel level(1.0f, 1.0f, 0.0f);
    level.setTarget(0.3f);
    level.update(0.001f);
    CHECK_THAT(level.smoothed(), WithinAbs(0.3, 1e-6));
}

TEST_CASE("Blackout forces zero brightness but smoothing keeps running") {
    OutputLevel level(1.0f, 1.0f, 50.0f);
    level.setBlackout(true);
    CHECK(level.brightness() == 0.0f);

    level.setTarget(0.5f);
    level.update(1.0f);
    CHECK(level.brightness() == 0.0f);

    level.setBlackout(false);
    CHECK_THAT(level.brightness(), WithinAbs(0.5, 1e-3));
}
```

- [ ] **Step 4: Run the build and confirm it fails**

Run: `cmake -S . -B build && cmake --build build -j`
Expected: FAIL: `src/output_level.cpp` not found / `output_level.h: No such file or directory`.

- [ ] **Step 5: Implement `src/output_level.h`**

```cpp
#pragma once

namespace sideshow {

// Output brightness: a smoothed, curved master level with a blackout override.
class OutputLevel {
public:
    OutputLevel(float initial, float curve, float smoothingMs);

    void setTarget(float value);   // clamped to [0, 1]
    void setBlackout(bool on);
    void update(float dtSeconds);  // moves the smoothed value toward the target

    float target() const { return target_; }
    float smoothed() const { return smoothed_; }
    bool blackout() const { return blackout_; }
    float brightness() const;      // smoothed^curve, or 0 during blackout

private:
    float target_;
    float smoothed_;
    float curve_;
    float tauSeconds_;
    bool blackout_ = false;
};

}  // namespace sideshow
```

- [ ] **Step 6: Implement `src/output_level.cpp`**

```cpp
#include "output_level.h"

#include <algorithm>
#include <cmath>

namespace sideshow {

OutputLevel::OutputLevel(float initial, float curve, float smoothingMs)
    : target_(std::clamp(initial, 0.0f, 1.0f)),
      smoothed_(target_),
      curve_(curve),
      tauSeconds_(smoothingMs / 1000.0f) {}

void OutputLevel::setTarget(float value) { target_ = std::clamp(value, 0.0f, 1.0f); }

void OutputLevel::setBlackout(bool on) { blackout_ = on; }

void OutputLevel::update(float dtSeconds) {
    if (tauSeconds_ <= 0.0f) {
        smoothed_ = target_;
        return;
    }
    if (dtSeconds <= 0.0f) return;
    const float alpha = 1.0f - std::exp(-dtSeconds / tauSeconds_);
    smoothed_ += (target_ - smoothed_) * alpha;
}

float OutputLevel::brightness() const {
    if (blackout_) return 0.0f;
    return std::pow(smoothed_, curve_);
}

}  // namespace sideshow
```

- [ ] **Step 7: Build and run the tests**

Run: `cmake --build build -j && ./build/sideshow_tests`
Expected: `All tests passed (… assertions in 5 test cases)`

- [ ] **Step 8: Commit**

```bash
git add .gitignore CMakeLists.txt src/output_level.h src/output_level.cpp tests/test_output_level.cpp
git commit -m "feat: add OutputLevel master fader smoothing with Catch2 test harness"
```

---

### Task 2: Command variant + CommandQueue

**Files:**
- Create: `src/command.h`, `tests/test_command_queue.cpp`
- Modify: `CMakeLists.txt` (test sources)

- [ ] **Step 1: Write the failing test `tests/test_command_queue.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

#include "command.h"

using namespace sideshow;

TEST_CASE("CommandQueue pops nothing when empty") {
    CommandQueue queue;
    CHECK_FALSE(queue.pop().has_value());
}

TEST_CASE("CommandQueue is FIFO and preserves payloads") {
    CommandQueue queue;
    queue.push(cmd::Next{});
    queue.push(cmd::Master{0.25f});
    queue.push(cmd::Playlist{"mellow"});

    auto first = queue.pop();
    REQUIRE(first);
    CHECK(std::holds_alternative<cmd::Next>(*first));

    auto second = queue.pop();
    REQUIRE(second);
    REQUIRE(std::holds_alternative<cmd::Master>(*second));
    CHECK(std::get<cmd::Master>(*second).value == 0.25f);

    auto third = queue.pop();
    REQUIRE(third);
    REQUIRE(std::holds_alternative<cmd::Playlist>(*third));
    CHECK(std::get<cmd::Playlist>(*third).name == "mellow");

    CHECK_FALSE(queue.pop().has_value());
}

TEST_CASE("CommandQueue accepts pushes from another thread") {
    CommandQueue queue;
    std::thread producer([&] {
        for (int i = 0; i < 1000; ++i) queue.push(cmd::Random{});
    });
    producer.join();
    int count = 0;
    while (queue.pop()) ++count;
    CHECK(count == 1000);
}
```

- [ ] **Step 2: Add the test file to `CMakeLists.txt`**

Replace the `add_executable(sideshow_tests ...)` block with:

```cmake
    add_executable(sideshow_tests
        tests/test_output_level.cpp
        tests/test_command_queue.cpp
    )
```

- [ ] **Step 3: Run the build and confirm it fails**

Run: `cmake --build build -j`
Expected: FAIL: `command.h: No such file or directory`.

- [ ] **Step 4: Implement `src/command.h`**

```cpp
#pragma once

#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <variant>

namespace sideshow {

namespace cmd {
struct Next {};
struct Prev {};
struct Random {};
struct Lock { bool on; };
struct Playlist { std::string name; };
struct Preset { std::string name; };
struct Blackout { bool on; };
struct Master { float value; };
struct Duration { float seconds; };
struct BeatSensitivity { float value; };
struct HardCut { bool on; };
}  // namespace cmd

using Command = std::variant<cmd::Next, cmd::Prev, cmd::Random, cmd::Lock, cmd::Playlist,
                             cmd::Preset, cmd::Blackout, cmd::Master, cmd::Duration,
                             cmd::BeatSensitivity, cmd::HardCut>;

// Multi-producer queue drained by the main loop between frames.
class CommandQueue {
public:
    void push(Command command) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(command));
    }

    std::optional<Command> pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        Command command = std::move(queue_.front());
        queue_.pop();
        return command;
    }

private:
    std::mutex mutex_;
    std::queue<Command> queue_;
};

}  // namespace sideshow
```

- [ ] **Step 5: Build and run the tests**

Run: `cmake --build build -j && ./build/sideshow_tests`
Expected: `All tests passed`

- [ ] **Step 6: Commit**

```bash
git add src/command.h tests/test_command_queue.cpp CMakeLists.txt
git commit -m "feat: add Command variant and thread-safe CommandQueue"
```

---

### Task 3: OSC message parsing

**Files:**
- Create: `src/osc_parse.h`, `src/osc_parse.cpp`, `tests/test_osc_parse.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing test `tests/test_osc_parse.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "osc_parse.h"

using namespace sideshow;

namespace {
std::optional<Command> parse(const std::string& path, std::vector<OscArg> args = {}) {
    return parseOscMessage("panel-03", path, args);
}
}  // namespace

TEST_CASE("OSC: the 'all' prefix and this panel's prefix are accepted") {
    auto all = parse("/sideshow/all/next");
    REQUIRE(all);
    CHECK(std::holds_alternative<cmd::Next>(*all));

    auto mine = parse("/sideshow/panel-03/prev");
    REQUIRE(mine);
    CHECK(std::holds_alternative<cmd::Prev>(*mine));
}

TEST_CASE("OSC: other panels, other prefixes, and unknown commands are ignored") {
    CHECK_FALSE(parse("/sideshow/panel-04/next"));
    CHECK_FALSE(parse("/other/all/next"));
    CHECK_FALSE(parse("/sideshow/all/explode"));
    CHECK_FALSE(parse("/sideshow/all"));
    CHECK_FALSE(parse("/sideshow/all/next/extra"));
}

TEST_CASE("OSC: argument count must match") {
    CHECK_FALSE(parse("/sideshow/all/next", {int32_t{1}}));
    CHECK_FALSE(parse("/sideshow/all/master"));
    CHECK_FALSE(parse("/sideshow/all/master", {0.5f, 0.5f}));
}

TEST_CASE("OSC: toggles accept ints and floats") {
    auto lockInt = parse("/sideshow/all/lock", {int32_t{1}});
    REQUIRE(lockInt);
    CHECK(std::get<cmd::Lock>(*lockInt).on);

    auto blackoutFloat = parse("/sideshow/all/blackout", {0.0f});
    REQUIRE(blackoutFloat);
    CHECK_FALSE(std::get<cmd::Blackout>(*blackoutFloat).on);

    auto hardcut = parse("/sideshow/all/hardcut", {1.0f});
    REQUIRE(hardcut);
    CHECK(std::get<cmd::HardCut>(*hardcut).on);

    CHECK_FALSE(parse("/sideshow/all/lock", {std::string("yes")}));
}

TEST_CASE("OSC: master accepts floats and ints, clamped to 0..1") {
    auto half = parse("/sideshow/all/master", {0.5f});
    REQUIRE(half);
    CHECK(std::get<cmd::Master>(*half).value == 0.5f);

    auto fromInt = parse("/sideshow/all/master", {int32_t{1}});
    REQUIRE(fromInt);
    CHECK(std::get<cmd::Master>(*fromInt).value == 1.0f);

    auto tooHigh = parse("/sideshow/all/master", {1.7f});
    REQUIRE(tooHigh);
    CHECK(std::get<cmd::Master>(*tooHigh).value == 1.0f);

    auto tooLow = parse("/sideshow/all/master", {-2.0f});
    REQUIRE(tooLow);
    CHECK(std::get<cmd::Master>(*tooLow).value == 0.0f);
}

TEST_CASE("OSC: non-finite floats are rejected") {
    CHECK_FALSE(parse("/sideshow/all/master", {std::numeric_limits<float>::quiet_NaN()}));
    CHECK_FALSE(parse("/sideshow/all/duration", {std::numeric_limits<float>::infinity()}));
}

TEST_CASE("OSC: duration and beat sensitivity are clamped") {
    auto shortDuration = parse("/sideshow/all/duration", {0.0f});
    REQUIRE(shortDuration);
    CHECK(std::get<cmd::Duration>(*shortDuration).seconds == 1.0f);

    auto longDuration = parse("/sideshow/all/duration", {int32_t{99999}});
    REQUIRE(longDuration);
    CHECK(std::get<cmd::Duration>(*longDuration).seconds == 3600.0f);

    auto beat = parse("/sideshow/all/beat_sensitivity", {9.0f});
    REQUIRE(beat);
    CHECK(std::get<cmd::BeatSensitivity>(*beat).value == 2.0f);
}

TEST_CASE("OSC: playlist and preset need a non-empty string") {
    auto playlist = parse("/sideshow/all/playlist", {std::string("mellow")});
    REQUIRE(playlist);
    CHECK(std::get<cmd::Playlist>(*playlist).name == "mellow");

    auto preset = parse("/sideshow/panel-03/preset", {std::string("Geiss - Swirl")});
    REQUIRE(preset);
    CHECK(std::get<cmd::Preset>(*preset).name == "Geiss - Swirl");

    CHECK_FALSE(parse("/sideshow/all/playlist", {int32_t{3}}));
    CHECK_FALSE(parse("/sideshow/all/preset", {std::string("")}));
}
```

- [ ] **Step 2: Add sources to `CMakeLists.txt`**

Replace the `add_library(sideshow_core STATIC ...)` block with:

```cmake
add_library(sideshow_core STATIC
    src/output_level.cpp
    src/osc_parse.cpp
)
```

and the `add_executable(sideshow_tests ...)` block with:

```cmake
    add_executable(sideshow_tests
        tests/test_output_level.cpp
        tests/test_command_queue.cpp
        tests/test_osc_parse.cpp
    )
```

- [ ] **Step 3: Run the build and confirm it fails**

Run: `cmake -S . -B build && cmake --build build -j`
Expected: FAIL: `osc_parse.h: No such file or directory` / `src/osc_parse.cpp` missing.

- [ ] **Step 4: Implement `src/osc_parse.h`**

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "command.h"

namespace sideshow {

using OscArg = std::variant<int32_t, float, std::string>;

inline constexpr const char* kPingAddress = "/sideshow/ping";
inline constexpr const char* kPongAddress = "/sideshow/pong";

// Maps "/sideshow/{all|<panelId>}/<command>" plus arguments to a Command.
// Returns nullopt for anything not addressed to this panel or not valid.
std::optional<Command> parseOscMessage(const std::string& panelId, const std::string& path,
                                       const std::vector<OscArg>& args);

}  // namespace sideshow
```

- [ ] **Step 5: Implement `src/osc_parse.cpp`**

```cpp
#include "osc_parse.h"

#include <algorithm>
#include <cmath>

namespace sideshow {
namespace {

const std::string kPrefix = "/sideshow/";

std::optional<bool> asBool(const OscArg& arg) {
    if (const auto* i = std::get_if<int32_t>(&arg)) return *i != 0;
    if (const auto* f = std::get_if<float>(&arg)) return *f != 0.0f;
    return std::nullopt;
}

std::optional<float> asFloat(const OscArg& arg) {
    if (const auto* f = std::get_if<float>(&arg)) {
        if (!std::isfinite(*f)) return std::nullopt;
        return *f;
    }
    if (const auto* i = std::get_if<int32_t>(&arg)) return static_cast<float>(*i);
    return std::nullopt;
}

std::optional<std::string> asNonEmptyString(const OscArg& arg) {
    if (const auto* s = std::get_if<std::string>(&arg); s && !s->empty()) return *s;
    return std::nullopt;
}

}  // namespace

std::optional<Command> parseOscMessage(const std::string& panelId, const std::string& path,
                                       const std::vector<OscArg>& args) {
    if (path.compare(0, kPrefix.size(), kPrefix) != 0) return std::nullopt;
    const std::string rest = path.substr(kPrefix.size());
    const auto slash = rest.find('/');
    if (slash == std::string::npos) return std::nullopt;
    const std::string target = rest.substr(0, slash);
    const std::string name = rest.substr(slash + 1);
    if (target != "all" && target != panelId) return std::nullopt;

    if (args.empty()) {
        if (name == "next") return cmd::Next{};
        if (name == "prev") return cmd::Prev{};
        if (name == "random") return cmd::Random{};
        return std::nullopt;
    }
    if (args.size() != 1) return std::nullopt;
    const OscArg& arg = args[0];

    if (name == "lock") {
        if (auto b = asBool(arg)) return cmd::Lock{*b};
    } else if (name == "blackout") {
        if (auto b = asBool(arg)) return cmd::Blackout{*b};
    } else if (name == "hardcut") {
        if (auto b = asBool(arg)) return cmd::HardCut{*b};
    } else if (name == "master") {
        if (auto f = asFloat(arg)) return cmd::Master{std::clamp(*f, 0.0f, 1.0f)};
    } else if (name == "duration") {
        if (auto f = asFloat(arg)) return cmd::Duration{std::clamp(*f, 1.0f, 3600.0f)};
    } else if (name == "beat_sensitivity") {
        if (auto f = asFloat(arg)) return cmd::BeatSensitivity{std::clamp(*f, 0.0f, 2.0f)};
    } else if (name == "playlist") {
        if (auto s = asNonEmptyString(arg)) return cmd::Playlist{*s};
    } else if (name == "preset") {
        if (auto s = asNonEmptyString(arg)) return cmd::Preset{*s};
    }
    return std::nullopt;
}

}  // namespace sideshow
```

- [ ] **Step 6: Build and run the tests**

Run: `cmake --build build -j && ./build/sideshow_tests`
Expected: `All tests passed`

- [ ] **Step 7: Commit**

```bash
git add src/osc_parse.h src/osc_parse.cpp tests/test_osc_parse.cpp CMakeLists.txt
git commit -m "feat: parse OSC addresses and arguments into commands"
```

---

### Task 4: AudioRing buffer

**Files:**
- Create: `src/audio_ring.h`, `src/audio_ring.cpp`, `tests/test_audio_ring.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing test `tests/test_audio_ring.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "audio_ring.h"

using sideshow::AudioRing;

TEST_CASE("AudioRing returns samples in FIFO order") {
    AudioRing ring(8);
    const float in[] = {1, 2, 3, 4};
    ring.write(in, 4);
    CHECK(ring.size() == 4);

    std::vector<float> out(8);
    CHECK(ring.read(out.data(), out.size()) == 4);
    CHECK(std::vector<float>(out.begin(), out.begin() + 4) == std::vector<float>{1, 2, 3, 4});
    CHECK(ring.size() == 0);
}

TEST_CASE("AudioRing reads at most maxCount") {
    AudioRing ring(8);
    const float in[] = {1, 2, 3, 4, 5, 6};
    ring.write(in, 6);

    std::vector<float> out(4);
    CHECK(ring.read(out.data(), 4) == 4);
    CHECK(out == std::vector<float>{1, 2, 3, 4});
    CHECK(ring.read(out.data(), 4) == 2);
    CHECK(out[0] == 5);
    CHECK(out[1] == 6);
}

TEST_CASE("AudioRing drops the oldest samples when full") {
    AudioRing ring(4);
    const float first[] = {1, 2, 3, 4};
    const float second[] = {5, 6};
    ring.write(first, 4);
    ring.write(second, 2);
    CHECK(ring.size() == 4);

    std::vector<float> out(4);
    CHECK(ring.read(out.data(), 4) == 4);
    CHECK(out == std::vector<float>{3, 4, 5, 6});
}

TEST_CASE("AudioRing keeps only the newest samples of an oversized write") {
    AudioRing ring(4);
    const float in[] = {1, 2, 3, 4, 5, 6, 7, 8};
    ring.write(in, 8);

    std::vector<float> out(4);
    CHECK(ring.read(out.data(), 4) == 4);
    CHECK(out == std::vector<float>{5, 6, 7, 8});
}
```

- [ ] **Step 2: Add sources to `CMakeLists.txt`**

```cmake
add_library(sideshow_core STATIC
    src/output_level.cpp
    src/osc_parse.cpp
    src/audio_ring.cpp
)
```

```cmake
    add_executable(sideshow_tests
        tests/test_output_level.cpp
        tests/test_command_queue.cpp
        tests/test_osc_parse.cpp
        tests/test_audio_ring.cpp
    )
```

- [ ] **Step 3: Run the build and confirm it fails**

Run: `cmake -S . -B build && cmake --build build -j`
Expected: FAIL: `audio_ring.h: No such file or directory`.

- [ ] **Step 4: Implement `src/audio_ring.h`**

```cpp
#pragma once

#include <cstddef>
#include <mutex>
#include <vector>

namespace sideshow {

// Fixed-capacity float FIFO shared by the audio callback (writer) and main loop (reader).
// When full, the oldest samples are dropped. Keep writes and reads in whole stereo frames
// (even counts) with an even capacity so channel interleaving is preserved.
class AudioRing {
public:
    explicit AudioRing(size_t capacity);

    void write(const float* data, size_t count);
    size_t read(float* out, size_t maxCount);
    size_t size() const;

private:
    mutable std::mutex mutex_;
    std::vector<float> buffer_;
    size_t head_ = 0;  // index of the oldest sample
    size_t size_ = 0;
};

}  // namespace sideshow
```

- [ ] **Step 5: Implement `src/audio_ring.cpp`**

```cpp
#include "audio_ring.h"

#include <algorithm>

namespace sideshow {

AudioRing::AudioRing(size_t capacity) : buffer_(capacity) {}

void AudioRing::write(const float* data, size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t capacity = buffer_.size();
    if (capacity == 0) return;
    if (count >= capacity) {
        std::copy(data + (count - capacity), data + count, buffer_.begin());
        head_ = 0;
        size_ = capacity;
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        buffer_[(head_ + size_) % capacity] = data[i];
        if (size_ < capacity) {
            ++size_;
        } else {
            head_ = (head_ + 1) % capacity;
        }
    }
}

size_t AudioRing::read(float* out, size_t maxCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t n = std::min(maxCount, size_);
    for (size_t i = 0; i < n; ++i) {
        out[i] = buffer_[(head_ + i) % buffer_.size()];
    }
    if (n > 0) {
        head_ = (head_ + n) % buffer_.size();
        size_ -= n;
    }
    return n;
}

size_t AudioRing::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}

}  // namespace sideshow
```

- [ ] **Step 6: Build and run the tests**

Run: `cmake --build build -j && ./build/sideshow_tests`
Expected: `All tests passed`

- [ ] **Step 7: Commit**

```bash
git add src/audio_ring.h src/audio_ring.cpp tests/test_audio_ring.cpp CMakeLists.txt
git commit -m "feat: add AudioRing buffer for audio thread handoff"
```

---

### Task 5: Config loading and validation

**Files:**
- Create: `src/config.h`, `src/config.cpp`, `tests/test_config.cpp`
- Modify: `CMakeLists.txt` (toml++ FetchContent + sources)

- [ ] **Step 1: Write the failing test `tests/test_config.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "config.h"

using namespace sideshow;

namespace {
bool hasWarningContaining(const ConfigResult& result, const std::string& text) {
    for (const auto& w : result.warnings) {
        if (w.find(text) != std::string::npos) return true;
    }
    return false;
}
}  // namespace

TEST_CASE("Config: empty text gives all defaults and no warnings") {
    ConfigResult r = parseConfig("");
    CHECK(r.warnings.empty());
    CHECK(r.config.panelId == "panel-01");
    CHECK(r.config.display.width == 0);
    CHECK(r.config.display.height == 0);
    CHECK(r.config.display.maxWidth == 2560);
    CHECK(r.config.display.maxHeight == 1440);
    CHECK(r.config.display.fps == 60);
    CHECK(r.config.display.fullscreen);
    CHECK(r.config.audio.device.empty());
    CHECK(r.config.audio.sampleRate == 48000);
    CHECK(r.config.osc.port == 9000);
    CHECK(r.config.presets.root == "/opt/sideshow/presets");
    CHECK(r.config.presets.textures == "/opt/sideshow/textures");
    CHECK(r.config.presets.playlist == "default");
    CHECK(r.config.presets.duration == 30.0);
    CHECK(r.config.presets.softCutDuration == 3.0);
    CHECK_FALSE(r.config.presets.hardcutEnabled);
    CHECK(r.config.presets.beatSensitivity == 1.0f);
    CHECK(r.config.presets.shuffle);
    CHECK(r.config.output.masterDefault == 1.0f);
    CHECK(r.config.output.masterCurve == 2.0f);
    CHECK(r.config.output.masterSmoothingMs == 50.0f);
}

TEST_CASE("Config: values override defaults") {
    ConfigResult r = parseConfig(R"(
panel_id = "panel-07"

[display]
width = 2560
height = 1440
max_width = 1920
max_height = 1080
fullscreen = false

[audio]
device = "USB"

[osc]
port = 9100

[presets]
root = "deps/presets"
playlist = "fractal"
duration = 12
hardcut_enabled = true
beat_sensitivity = 1.5
shuffle = false

[output]
master_curve = 1.0
master_smoothing_ms = 0
)");
    CHECK(r.warnings.empty());
    CHECK(r.config.panelId == "panel-07");
    CHECK(r.config.display.width == 2560);
    CHECK(r.config.display.height == 1440);
    CHECK(r.config.display.maxWidth == 1920);
    CHECK(r.config.display.maxHeight == 1080);
    CHECK_FALSE(r.config.display.fullscreen);
    CHECK(r.config.audio.device == "USB");
    CHECK(r.config.osc.port == 9100);
    CHECK(r.config.presets.root == "deps/presets");
    CHECK(r.config.presets.playlist == "fractal");
    CHECK(r.config.presets.duration == 12.0);  // integer accepted for a float key
    CHECK(r.config.presets.hardcutEnabled);
    CHECK(r.config.presets.beatSensitivity == 1.5f);
    CHECK_FALSE(r.config.presets.shuffle);
    CHECK(r.config.output.masterCurve == 1.0f);
    CHECK(r.config.output.masterSmoothingMs == 0.0f);
}

TEST_CASE("Config: a wrong type falls back to the default for that key only") {
    ConfigResult r = parseConfig(R"(
panel_id = 5
[osc]
port = "nine thousand"
[display]
fps = 30
)");
    CHECK(r.config.panelId == "panel-01");
    CHECK(r.config.osc.port == 9000);
    CHECK(r.config.display.fps == 30);
    CHECK(hasWarningContaining(r, "panel_id"));
    CHECK(hasWarningContaining(r, "osc.port"));
}

TEST_CASE("Config: out-of-range values fall back to defaults") {
    ConfigResult r = parseConfig(R"(
panel_id = "Panel 1!"
[display]
max_width = 10
[osc]
port = 70000
[output]
master_default = 1.5
)");
    CHECK(r.config.panelId == "panel-01");
    CHECK(r.config.osc.port == 9000);
    CHECK(r.config.output.masterDefault == 1.0f);
    CHECK(r.config.display.maxWidth == 2560);
    CHECK(hasWarningContaining(r, "display.max_width"));
    CHECK(hasWarningContaining(r, "panel_id"));
    CHECK(hasWarningContaining(r, "osc.port"));
    CHECK(hasWarningContaining(r, "output.master_default"));
}

TEST_CASE("Config: unparseable TOML gives defaults and a warning") {
    ConfigResult r = parseConfig("this is = = not toml");
    CHECK(r.config.osc.port == 9000);
    CHECK(hasWarningContaining(r, "parse error"));
}

TEST_CASE("Config: a missing file gives defaults and a warning") {
    ConfigResult r = loadConfigFile("/nonexistent/sideshow/config.toml");
    CHECK(r.config.panelId == "panel-01");
    CHECK(hasWarningContaining(r, "not found"));
}
```

- [ ] **Step 2: Update `CMakeLists.txt`**

After `include(FetchContent)` add:

```cmake
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(tomlplusplus)
```

Replace the core library block with:

```cmake
add_library(sideshow_core STATIC
    src/output_level.cpp
    src/osc_parse.cpp
    src/audio_ring.cpp
    src/config.cpp
)
target_include_directories(sideshow_core PUBLIC src)
target_compile_options(sideshow_core PRIVATE -Wall -Wextra)
target_link_libraries(sideshow_core PUBLIC tomlplusplus::tomlplusplus)
```

and the test executable block with:

```cmake
    add_executable(sideshow_tests
        tests/test_output_level.cpp
        tests/test_command_queue.cpp
        tests/test_osc_parse.cpp
        tests/test_audio_ring.cpp
        tests/test_config.cpp
    )
```

- [ ] **Step 3: Run the build and confirm it fails**

Run: `cmake -S . -B build && cmake --build build -j`
Expected: FAIL: `config.h: No such file or directory`.

- [ ] **Step 4: Implement `src/config.h`**

```cpp
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sideshow {

struct Config {
    std::string panelId = "panel-01";

    struct Display {
        int width = 0;         // 0 = automatic; >0 = explicit mode (if the screen supports it)
        int height = 0;
        int maxWidth = 2560;   // cap for automatic selection
        int maxHeight = 1440;
        int fps = 60;
        bool fullscreen = true;
    } display;

    struct Audio {
        std::string device;  // "" = first capture device; else substring of SDL device name
        int sampleRate = 48000;
    } audio;

    struct Osc {
        int port = 9000;
    } osc;

    struct Presets {
        std::string root = "/opt/sideshow/presets";
        std::string textures = "/opt/sideshow/textures";
        std::string playlist = "default";
        double duration = 30.0;
        double softCutDuration = 3.0;
        bool hardcutEnabled = false;
        float beatSensitivity = 1.0f;
        bool shuffle = true;
    } presets;

    struct Output {
        float masterDefault = 1.0f;
        float masterCurve = 2.0f;
        float masterSmoothingMs = 50.0f;
    } output;
};

struct ConfigResult {
    Config config;
    std::vector<std::string> warnings;
};

// Never fails: anything missing or invalid falls back to its default and adds a warning.
ConfigResult parseConfig(std::string_view tomlText);
ConfigResult loadConfigFile(const std::string& path);

bool isValidPanelId(const std::string& id);

}  // namespace sideshow
```

- [ ] **Step 5: Implement `src/config.cpp`**

```cpp
#include "config.h"

#include <fstream>
#include <optional>
#include <sstream>
#include <type_traits>

#include <toml++/toml.hpp>

namespace sideshow {
namespace {

std::string keyName(std::string_view section, std::string_view key) {
    if (section.empty()) return std::string(key);
    return std::string(section) + "." + std::string(key);
}

template <typename T, typename Valid>
void readKey(const toml::table& root, std::string_view section, std::string_view key, T& out,
             Valid valid, std::vector<std::string>& warnings) {
    toml::node_view<const toml::node> view = section.empty() ? root[key] : root[section][key];
    if (!view) return;  // absent: keep default

    std::optional<T> value;
    if constexpr (std::is_same_v<T, float>) {
        if (auto d = view.template value<double>()) value = static_cast<float>(*d);
    } else {
        value = view.template value<T>();
    }

    if (!value) {
        warnings.push_back(keyName(section, key) + ": wrong type, using default");
        return;
    }
    if (!valid(*value)) {
        warnings.push_back(keyName(section, key) + ": invalid value, using default");
        return;
    }
    out = *value;
}

}  // namespace

bool isValidPanelId(const std::string& id) {
    if (id.empty() || id.size() > 32 || id[0] == '-') return false;
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) return false;
    }
    return true;
}

ConfigResult parseConfig(std::string_view tomlText) {
    ConfigResult result;
    toml::table root;
    try {
        root = toml::parse(tomlText);
    } catch (const toml::parse_error& e) {
        result.warnings.push_back("config parse error: " + std::string(e.description()) +
                                  ", using all defaults");
        return result;
    }

    Config& c = result.config;
    auto& w = result.warnings;
    auto any = [](const auto&) { return true; };

    readKey(root, "", "panel_id", c.panelId, isValidPanelId, w);

    readKey(root, "display", "width", c.display.width, [](int v) { return v >= 0 && v <= 7680; }, w);
    readKey(root, "display", "height", c.display.height, [](int v) { return v >= 0 && v <= 4320; }, w);
    readKey(root, "display", "max_width", c.display.maxWidth,
            [](int v) { return v >= 320 && v <= 7680; }, w);
    readKey(root, "display", "max_height", c.display.maxHeight,
            [](int v) { return v >= 240 && v <= 4320; }, w);
    readKey(root, "display", "fps", c.display.fps, [](int v) { return v >= 1 && v <= 240; }, w);
    readKey(root, "display", "fullscreen", c.display.fullscreen, any, w);

    readKey(root, "audio", "device", c.audio.device, any, w);
    readKey(root, "audio", "sample_rate", c.audio.sampleRate,
            [](int v) { return v >= 8000 && v <= 192000; }, w);

    readKey(root, "osc", "port", c.osc.port, [](int v) { return v >= 1 && v <= 65535; }, w);

    auto nonEmpty = [](const std::string& s) { return !s.empty(); };
    readKey(root, "presets", "root", c.presets.root, nonEmpty, w);
    readKey(root, "presets", "textures", c.presets.textures, nonEmpty, w);
    readKey(root, "presets", "playlist", c.presets.playlist, nonEmpty, w);
    readKey(root, "presets", "duration", c.presets.duration,
            [](double v) { return v >= 1.0 && v <= 3600.0; }, w);
    readKey(root, "presets", "soft_cut_duration", c.presets.softCutDuration,
            [](double v) { return v >= 0.0 && v <= 60.0; }, w);
    readKey(root, "presets", "hardcut_enabled", c.presets.hardcutEnabled, any, w);
    readKey(root, "presets", "beat_sensitivity", c.presets.beatSensitivity,
            [](float v) { return v >= 0.0f && v <= 2.0f; }, w);  // projectM clamps to 0..2
    readKey(root, "presets", "shuffle", c.presets.shuffle, any, w);

    readKey(root, "output", "master_default", c.output.masterDefault,
            [](float v) { return v >= 0.0f && v <= 1.0f; }, w);
    readKey(root, "output", "master_curve", c.output.masterCurve,
            [](float v) { return v >= 0.1f && v <= 10.0f; }, w);
    readKey(root, "output", "master_smoothing_ms", c.output.masterSmoothingMs,
            [](float v) { return v >= 0.0f && v <= 5000.0f; }, w);

    return result;
}

ConfigResult loadConfigFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        ConfigResult result;
        result.warnings.push_back("config file not found: " + path + ", using defaults");
        return result;
    }
    std::stringstream text;
    text << in.rdbuf();
    return parseConfig(text.str());
}

}  // namespace sideshow
```

- [ ] **Step 6: Build and run the tests**

Run: `cmake --build build -j && ./build/sideshow_tests`
Expected: `All tests passed`. If `duration = 12` (integer) fails the "values override defaults" case, toml++'s `value<double>()` is not converting integers on this version: change the non-float branch in `readKey` to special-case `double` exactly like `float` (`if (auto d = view.template value<double>())`) — `value<double>()` on an integer node is documented as permitted in toml++ 3.x, so this should not be needed.

- [ ] **Step 7: Commit**

```bash
git add src/config.h src/config.cpp tests/test_config.cpp CMakeLists.txt
git commit -m "feat: load TOML config with per-key validation and defaults"
```

---

### Task 6: PresetCatalog

**Files:**
- Create: `src/preset_catalog.h`, `src/preset_catalog.cpp`, `tests/test_preset_catalog.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing test `tests/test_preset_catalog.cpp`**

```cpp
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
```

- [ ] **Step 2: Add sources to `CMakeLists.txt`**

```cmake
add_library(sideshow_core STATIC
    src/output_level.cpp
    src/osc_parse.cpp
    src/audio_ring.cpp
    src/config.cpp
    src/preset_catalog.cpp
)
```

```cmake
    add_executable(sideshow_tests
        tests/test_output_level.cpp
        tests/test_command_queue.cpp
        tests/test_osc_parse.cpp
        tests/test_audio_ring.cpp
        tests/test_config.cpp
        tests/test_preset_catalog.cpp
    )
```

- [ ] **Step 3: Run the build and confirm it fails**

Run: `cmake -S . -B build && cmake --build build -j`
Expected: FAIL: `preset_catalog.h: No such file or directory`.

- [ ] **Step 4: Implement `src/preset_catalog.h`**

```cpp
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
```

- [ ] **Step 5: Implement `src/preset_catalog.cpp`**

```cpp
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
```

- [ ] **Step 6: Build and run the tests**

Run: `cmake --build build -j && ./build/sideshow_tests`
Expected: `All tests passed`

- [ ] **Step 7: Commit**

```bash
git add src/preset_catalog.h src/preset_catalog.cpp tests/test_preset_catalog.cpp CMakeLists.txt
git commit -m "feat: index playlists and presets with bad-preset exclusion"
```

---

### Task 7: Display mode picker

**Files:**
- Create: `src/display_mode.h`, `src/display_mode.cpp`, `tests/test_display_mode.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing test `tests/test_display_mode.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "display_mode.h"

using namespace sideshow;

namespace {
DisplayMode pickedMode(const std::vector<DisplayMode>& modes, const DisplayMode& native,
                       int maxWidth = 2560, int maxHeight = 1440) {
    auto index = pickAutoMode(modes, native, maxWidth, maxHeight);
    REQUIRE(index);
    return modes[*index];
}
}  // namespace

TEST_CASE("Auto mode uses the native size when it fits the cap") {
    const std::vector<DisplayMode> modes = {
        {3840, 2160, 60}, {2560, 1440, 60}, {1920, 1080, 60}, {1280, 720, 60}};
    DisplayMode m = pickedMode(modes, {2560, 1440, 60});
    CHECK(m.width == 2560);
    CHECK(m.height == 1440);

    DisplayMode small = pickedMode(modes, {1920, 1080, 60});
    CHECK(small.width == 1920);
}

TEST_CASE("Auto mode steps a 4K screen down to the largest mode within the cap") {
    const std::vector<DisplayMode> modes = {
        {3840, 2160, 60}, {2560, 1440, 60}, {1920, 1080, 60}};
    DisplayMode m = pickedMode(modes, {3840, 2160, 60});
    CHECK(m.width == 2560);
    CHECK(m.height == 1440);

    DisplayMode capped = pickedMode(modes, {3840, 2160, 60}, 1920, 1080);
    CHECK(capped.width == 1920);
}

TEST_CASE("Auto mode prefers the native aspect ratio over a larger mode") {
    // A 4K TV without 1440p: 1920x1200 is larger than 1920x1080 but 16:10.
    const std::vector<DisplayMode> modes = {
        {3840, 2160, 60}, {1920, 1200, 60}, {1920, 1080, 60}, {1280, 720, 60}};
    DisplayMode m = pickedMode(modes, {3840, 2160, 60});
    CHECK(m.width == 1920);
    CHECK(m.height == 1080);
}

TEST_CASE("Auto mode prefers 60 Hz over higher or lower refresh rates") {
    const std::vector<DisplayMode> highRefresh = {
        {2560, 1440, 144}, {2560, 1440, 120}, {2560, 1440, 60}};
    CHECK(pickedMode(highRefresh, {2560, 1440, 144}).refreshRate == 60);

    const std::vector<DisplayMode> tv = {{1920, 1080, 24}, {1920, 1080, 50}, {1920, 1080, 60}};
    CHECK(pickedMode(tv, {1920, 1080, 60}).refreshRate == 60);

    const std::vector<DisplayMode> pal = {{1920, 1080, 24}, {1920, 1080, 50}};
    CHECK(pickedMode(pal, {1920, 1080, 50}).refreshRate == 50);
}

TEST_CASE("Auto mode accepts unknown refresh rates") {
    const std::vector<DisplayMode> modes = {{1920, 1080, 0}};
    CHECK(pickedMode(modes, {1920, 1080, 0}).width == 1920);
}

TEST_CASE("Auto mode returns nothing when no mode fits the cap") {
    const std::vector<DisplayMode> modes = {{3840, 2160, 60}, {3840, 2160, 30}};
    CHECK_FALSE(pickAutoMode(modes, {3840, 2160, 60}, 2560, 1440));
}

TEST_CASE("Exact mode lookup finds the size with the best refresh rate") {
    const std::vector<DisplayMode> modes = {
        {2560, 1440, 144}, {2560, 1440, 60}, {1920, 1080, 60}};
    auto index = findExactMode(modes, 2560, 1440);
    REQUIRE(index);
    CHECK(modes[*index].refreshRate == 60);
    CHECK_FALSE(findExactMode(modes, 1234, 567));
}
```

- [ ] **Step 2: Add sources to `CMakeLists.txt`**

```cmake
add_library(sideshow_core STATIC
    src/output_level.cpp
    src/osc_parse.cpp
    src/audio_ring.cpp
    src/config.cpp
    src/preset_catalog.cpp
    src/display_mode.cpp
)
```

```cmake
    add_executable(sideshow_tests
        tests/test_output_level.cpp
        tests/test_command_queue.cpp
        tests/test_osc_parse.cpp
        tests/test_audio_ring.cpp
        tests/test_config.cpp
        tests/test_preset_catalog.cpp
        tests/test_display_mode.cpp
    )
```

- [ ] **Step 3: Run the build and confirm it fails**

Run: `cmake -S . -B build && cmake --build build -j`
Expected: FAIL: `display_mode.h: No such file or directory`.

- [ ] **Step 4: Implement `src/display_mode.h`**

```cpp
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace sideshow {

struct DisplayMode {
    int width;
    int height;
    int refreshRate;  // Hz; 0 = unknown
};

// Index of the mode with exactly width x height and the best refresh rate, if any.
std::optional<size_t> findExactMode(const std::vector<DisplayMode>& modes, int width, int height);

// Automatic choice: the native size if it fits within maxWidth x maxHeight, otherwise the
// largest mode that fits, preferring the native aspect ratio. Among modes of the chosen size,
// 50-60 Hz is preferred (60 over 50). Returns nullopt if no mode fits the cap.
std::optional<size_t> pickAutoMode(const std::vector<DisplayMode>& modes, const DisplayMode& native,
                                   int maxWidth, int maxHeight);

}  // namespace sideshow
```

- [ ] **Step 5: Implement `src/display_mode.cpp`**

```cpp
#include "display_mode.h"

#include <cmath>

namespace sideshow {
namespace {

// Lower is better. projectM renders once per vsync, so >60 Hz costs GPU for no benefit.
int refreshPenalty(int hz) {
    if (hz == 0) return 0;                     // unknown: assume fine
    if (hz >= 50 && hz <= 61) return 61 - hz;  // prefer 60 over 50
    if (hz > 61) return 100 + hz;
    return 200 + (50 - hz);
}

bool sameAspect(const DisplayMode& a, const DisplayMode& b) {
    if (a.height == 0 || b.height == 0) return false;
    const double ra = static_cast<double>(a.width) / a.height;
    const double rb = static_cast<double>(b.width) / b.height;
    return std::abs(ra - rb) / rb < 0.01;
}

bool fits(const DisplayMode& m, int maxWidth, int maxHeight) {
    return m.width <= maxWidth && m.height <= maxHeight;
}

}  // namespace

std::optional<size_t> findExactMode(const std::vector<DisplayMode>& modes, int width, int height) {
    std::optional<size_t> best;
    for (size_t i = 0; i < modes.size(); ++i) {
        if (modes[i].width != width || modes[i].height != height) continue;
        if (!best || refreshPenalty(modes[i].refreshRate) < refreshPenalty(modes[*best].refreshRate)) {
            best = i;
        }
    }
    return best;
}

std::optional<size_t> pickAutoMode(const std::vector<DisplayMode>& modes, const DisplayMode& native,
                                   int maxWidth, int maxHeight) {
    if (fits(native, maxWidth, maxHeight)) {
        if (auto index = findExactMode(modes, native.width, native.height)) return index;
    }

    std::optional<size_t> bestSize;
    for (size_t i = 0; i < modes.size(); ++i) {
        const DisplayMode& m = modes[i];
        if (!fits(m, maxWidth, maxHeight)) continue;
        if (!bestSize) {
            bestSize = i;
            continue;
        }
        const DisplayMode& b = modes[*bestSize];
        const bool mAspect = sameAspect(m, native);
        const bool bAspect = sameAspect(b, native);
        if (mAspect != bAspect) {
            if (mAspect) bestSize = i;
            continue;
        }
        if (static_cast<long>(m.width) * m.height > static_cast<long>(b.width) * b.height) {
            bestSize = i;
        }
    }
    if (!bestSize) return std::nullopt;
    return findExactMode(modes, modes[*bestSize].width, modes[*bestSize].height);
}

}  // namespace sideshow
```

- [ ] **Step 6: Build and run the tests**

Run: `cmake --build build -j && ./build/sideshow_tests`
Expected: `All tests passed`

- [ ] **Step 7: Commit**

```bash
git add src/display_mode.h src/display_mode.cpp tests/test_display_mode.cpp CMakeLists.txt
git commit -m "feat: pick display mode from native size with a resolution cap"
```

---

### Task 8: Dependency and preset scripts (macOS dev setup)

**Files:**
- Create: `setup/build_projectm.sh`, `setup/fetch_presets.sh`, `setup/dev-macos.sh`, `setup/config.dev.toml`

- [ ] **Step 1: Create `setup/build_projectm.sh`**

```bash
#!/usr/bin/env bash
# Build and install libprojectM + its playlist library at a pinned release.
# Usage: setup/build_projectm.sh <install-prefix> [--gles]
# Safe to re-run: exits early if the pinned version is already installed.
set -euo pipefail

PROJECTM_TAG="v4.1.7"
PREFIX="${1:?usage: build_projectm.sh <install-prefix> [--gles]}"
GLES=OFF
if [[ "${2:-}" == "--gles" ]]; then GLES=ON; fi

VERSION_FILE="$PREFIX/lib/cmake/projectM4/projectM4ConfigVersion.cmake"
if [[ -f "$VERSION_FILE" ]] && grep -q "set(PACKAGE_VERSION \"${PROJECTM_TAG#v}\")" "$VERSION_FILE"; then
    echo "libprojectM ${PROJECTM_TAG} already installed in $PREFIX"
    exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

git clone --depth 1 --branch "$PROJECTM_TAG" --recurse-submodules --shallow-submodules \
    https://github.com/projectM-visualizer/projectm.git "$WORK/src"

cmake -S "$WORK/src" -B "$WORK/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DENABLE_GLES="$GLES" \
    -DENABLE_PLAYLIST=ON \
    -DENABLE_SYSTEM_PROJECTM_EVAL=OFF \
    -DBUILD_TESTING=OFF

cmake --build "$WORK/build" --parallel "$(getconf _NPROCESSORS_ONLN)"
cmake --install "$WORK/build"
echo "Installed libprojectM ${PROJECTM_TAG} to $PREFIX"
```

- [ ] **Step 2: Create `setup/fetch_presets.sh`**

```bash
#!/usr/bin/env bash
# Fetch the projectM preset and texture packs at pinned commits and lay out playlists.
# Usage: setup/fetch_presets.sh <dest-dir>
# Result:
#   <dest>/packs/cream-of-the-crop   the preset pack
#   <dest>/textures                  the texture pack
#   <dest>/presets/default           symlink to the whole pack
#   <dest>/presets/<category>        symlink per pack category (lowercase, spaces -> dashes)
# Safe to re-run. Custom playlists: create more directories (or symlinks) under <dest>/presets.
set -euo pipefail

DEST="${1:?usage: fetch_presets.sh <dest-dir>}"
PRESETS_REPO="https://github.com/projectM-visualizer/presets-cream-of-the-crop.git"
PRESETS_COMMIT="0180df21f5e0bd39b9060cc5de420ed2f1f9e509"
TEXTURES_REPO="https://github.com/projectM-visualizer/presets-milkdrop-texture-pack.git"
TEXTURES_COMMIT="6368812f27bc747b517218fbf89d21d59afce4d9"

fetch_pinned() {
    local repo="$1" commit="$2" dir="$3"
    if [[ -d "$dir/.git" ]] && [[ "$(git -C "$dir" rev-parse HEAD)" == "$commit" ]]; then
        echo "$dir already at ${commit:0:8}"
        return
    fi
    rm -rf "$dir"
    mkdir -p "$dir"
    git -C "$dir" init -q
    git -C "$dir" fetch -q --depth 1 "$repo" "$commit"
    git -C "$dir" checkout -q FETCH_HEAD
}

fetch_pinned "$PRESETS_REPO" "$PRESETS_COMMIT" "$DEST/packs/cream-of-the-crop"
fetch_pinned "$TEXTURES_REPO" "$TEXTURES_COMMIT" "$DEST/textures"

mkdir -p "$DEST/presets"
ln -sfn ../packs/cream-of-the-crop "$DEST/presets/default"
for category in "$DEST/packs/cream-of-the-crop"/*/; do
    name="$(basename "$category")"
    [[ "$name" == "! Transition" ]] && continue
    playlist="$(echo "$name" | tr '[:upper:] ' '[:lower:]-')"
    ln -sfn "../packs/cream-of-the-crop/$name" "$DEST/presets/$playlist"
done

echo "Playlists in $DEST/presets: $(ls "$DEST/presets" | tr '\n' ' ')"
```

- [ ] **Step 3: Create `setup/dev-macos.sh`**

```bash
#!/usr/bin/env bash
# One-time macOS development setup: Homebrew deps, projectM into ./deps/install,
# preset and texture packs into ./deps.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"

brew install cmake pkg-config sdl2 liblo
"$REPO_DIR/setup/build_projectm.sh" "$REPO_DIR/deps/install"
"$REPO_DIR/setup/fetch_presets.sh" "$REPO_DIR/deps"

cat <<EOF

Dev setup complete. Build and run the player with:
  cmake -S . -B build -DSIDESHOW_BUILD_PLAYER=ON -DCMAKE_PREFIX_PATH="$REPO_DIR/deps/install"
  cmake --build build -j
  ./build/sideshow-player --config setup/config.dev.toml
EOF
```

- [ ] **Step 4: Create `setup/config.dev.toml`** (paths relative to the repo root; run the player from there)

```toml
# Development config for macOS: windowed, presets from ./deps (see setup/dev-macos.sh).
panel_id = "dev"

[display]
width = 1280
height = 720
fps = 60
fullscreen = false

[audio]
device = ""
sample_rate = 48000

[osc]
port = 9000

[presets]
root = "deps/presets"
textures = "deps/textures"
playlist = "default"
duration = 15.0
soft_cut_duration = 3.0
hardcut_enabled = false
beat_sensitivity = 1.0
shuffle = true

[output]
master_default = 1.0
master_curve = 2.0
master_smoothing_ms = 50
```

- [ ] **Step 5: Make scripts executable and run the dev setup**

Run:
```bash
chmod +x setup/*.sh
setup/dev-macos.sh
```
Expected: ends with `Installed libprojectM v4.1.7 to …/deps/install`, `Playlists in …/deps/presets: dancer default drawing fractal geometric hypnotic particles reaction sparkle supernova waveform`, and the "Dev setup complete" message. Running `setup/dev-macos.sh` a second time prints `already installed` / `already at` and finishes quickly.

- [ ] **Step 6: Commit**

```bash
git add setup/build_projectm.sh setup/fetch_presets.sh setup/dev-macos.sh setup/config.dev.toml
git commit -m "build: add projectM build, preset fetch, and macOS dev setup scripts"
```

---

### Task 9: Display window + minimal player

**Files:**
- Create: `src/log.h`, `src/gl.h`, `src/display.h`, `src/display.cpp`, `src/main.cpp`
- Modify: `CMakeLists.txt` (player target)

- [ ] **Step 1: Create `src/log.h`**

```cpp
#pragma once

#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>

namespace sideshow {

// Line-atomic logging to stderr (journald adds timestamps on the Pi).
inline void logLine(const char* level, const std::string& message) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    std::cerr << level << message << '\n' << std::flush;
}

inline void logInfo(const std::string& message) { logLine("[info] ", message); }
inline void logWarn(const std::string& message) { logLine("[warn] ", message); }
inline void logError(const std::string& message) { logLine("[error] ", message); }

// Enabled with the environment variable SIDESHOW_DEBUG=1.
inline void logDebug(const std::string& message) {
    static const bool enabled = std::getenv("SIDESHOW_DEBUG") != nullptr;
    if (enabled) logLine("[debug] ", message);
}

}  // namespace sideshow
```

- [ ] **Step 2: Create `src/gl.h`**

```cpp
#pragma once

// SIDESHOW_GLES is defined by CMake on Linux (Raspberry Pi, OpenGL ES 3).
#if defined(SIDESHOW_GLES)
#include <GLES3/gl3.h>
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#error "Unsupported platform: build on macOS or with SIDESHOW_GLES (Linux/Raspberry Pi)"
#endif
```

- [ ] **Step 3: Create `src/display.h`**

```cpp
#pragma once

#include <SDL.h>

#include "config.h"

namespace sideshow {

// SDL window + OpenGL context. On the Pi (no X/Wayland) SDL uses KMS/DRM directly.
class Display {
public:
    explicit Display(const Config::Display& config);  // throws std::runtime_error
    ~Display();
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    void swap();
    int drawableWidth() const;
    int drawableHeight() const;

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
};

}  // namespace sideshow
```

- [ ] **Step 4: Create `src/display.cpp`** (in fullscreen, picks the screen mode with `display_mode.h`; windowed dev mode just opens a window)

```cpp
#include "display.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "display_mode.h"
#include "gl.h"
#include "log.h"

namespace sideshow {
namespace {

std::string describe(const DisplayMode& m) {
    return std::to_string(m.width) + "x" + std::to_string(m.height) + "@" +
           std::to_string(m.refreshRate) + "Hz";
}

// Reads the screen's modes (from EDID) and picks one per config: explicit size if the screen
// supports it, else the native size capped at max_width x max_height.
SDL_DisplayMode chooseFullscreenMode(const Config::Display& config) {
    if (SDL_GetNumVideoDisplays() < 1) {
        throw std::runtime_error("no connected display (systemd will retry)");
    }
    SDL_DisplayMode native{};
    if (SDL_GetDesktopDisplayMode(0, &native) != 0) {
        throw std::runtime_error(std::string("cannot read display mode: ") + SDL_GetError());
    }
    const DisplayMode nativeInfo{native.w, native.h, native.refresh_rate};

    std::vector<SDL_DisplayMode> sdlModes;
    std::vector<DisplayMode> modes;
    const int count = SDL_GetNumDisplayModes(0);
    for (int i = 0; i < count; ++i) {
        SDL_DisplayMode m{};
        if (SDL_GetDisplayMode(0, i, &m) == 0) {
            sdlModes.push_back(m);
            modes.push_back({m.w, m.h, m.refresh_rate});
        }
    }

    std::optional<size_t> pick;
    if (config.width > 0 && config.height > 0) {
        pick = findExactMode(modes, config.width, config.height);
        if (!pick) {
            logWarn("display mode " + std::to_string(config.width) + "x" +
                    std::to_string(config.height) +
                    " not supported by this screen; choosing automatically");
        }
    }
    if (!pick) pick = pickAutoMode(modes, nativeInfo, config.maxWidth, config.maxHeight);
    if (!pick) {
        logWarn("no display mode fits within " + std::to_string(config.maxWidth) + "x" +
                std::to_string(config.maxHeight) + "; using native " + describe(nativeInfo));
        return native;
    }
    logInfo("screen native " + describe(nativeInfo) + ", using " + describe(modes[*pick]));
    return sdlModes[*pick];
}

}  // namespace

Display::Display(const Config::Display& config) {
#ifdef SIDESHOW_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;
    int width = config.width > 0 ? config.width : 1280;  // windowed size (dev)
    int height = config.height > 0 ? config.height : 720;
    SDL_DisplayMode mode{};
    if (config.fullscreen) {
        mode = chooseFullscreenMode(config);
        width = mode.w;
        height = mode.h;
        flags |= SDL_WINDOW_FULLSCREEN;
    } else {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    window_ = SDL_CreateWindow("sideshow", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               width, height, flags);
    if (!window_) {
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }
    if (config.fullscreen && SDL_SetWindowDisplayMode(window_, &mode) != 0) {
        logWarn(std::string("SDL_SetWindowDisplayMode failed: ") + SDL_GetError());
    }
    context_ = SDL_GL_CreateContext(window_);
    if (!context_) {
        const std::string error = SDL_GetError();
        SDL_DestroyWindow(window_);
        throw std::runtime_error("SDL_GL_CreateContext failed: " + error);
    }
    if (SDL_GL_SetSwapInterval(1) != 0) {
        logWarn(std::string("vsync unavailable: ") + SDL_GetError());
    }
    SDL_ShowCursor(SDL_DISABLE);

    logInfo("display " + std::to_string(drawableWidth()) + "x" + std::to_string(drawableHeight()) +
            ", " + reinterpret_cast<const char*>(glGetString(GL_VERSION)));
}

Display::~Display() {
    if (context_) SDL_GL_DeleteContext(context_);
    if (window_) SDL_DestroyWindow(window_);
}

void Display::swap() { SDL_GL_SwapWindow(window_); }

int Display::drawableWidth() const {
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window_, &w, &h);
    return w;
}

int Display::drawableHeight() const {
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window_, &w, &h);
    return h;
}

}  // namespace sideshow
```

- [ ] **Step 5: Create a minimal `src/main.cpp`** (a dark-blue window, proves SDL + GL work)

```cpp
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstring>
#include <exception>
#include <string>

#include "config.h"
#include "display.h"
#include "gl.h"
#include "log.h"

using namespace sideshow;

namespace {

constexpr const char* kDefaultConfigPath = "/etc/sideshow/config.toml";

std::string configPathFromArgs(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0) return argv[i + 1];
    }
    return kDefaultConfigPath;
}

}  // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();
    const std::string configPath = configPathFromArgs(argc, argv);
    ConfigResult loaded = loadConfigFile(configPath);
    for (const auto& warning : loaded.warnings) logWarn(warning);
    const Config& config = loaded.config;
    logInfo("sideshow-player starting as " + config.panelId + " (config: " + configPath + ")");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        logError(std::string("SDL_Init failed (no connected display? systemd will retry): ") +
                 SDL_GetError());
        return 1;
    }

    int exitCode = 0;
    try {
        Display display(config.display);
        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) running = false;
            }
            glViewport(0, 0, display.drawableWidth(), display.drawableHeight());
            glClearColor(0.0f, 0.0f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            display.swap();
        }
    } catch (const std::exception& e) {
        logError(e.what());
        exitCode = 1;
    }
    SDL_Quit();
    return exitCode;
}
```

- [ ] **Step 6: Add the player target to `CMakeLists.txt`** (insert just before the `if(SIDESHOW_BUILD_TESTS)` block)

```cmake
if(SIDESHOW_BUILD_PLAYER)
    find_package(SDL2 REQUIRED)
    find_package(Threads REQUIRED)
    find_package(PkgConfig REQUIRED)

    if(APPLE)
        find_package(OpenGL REQUIRED)
        set(SIDESHOW_GL_LIBS OpenGL::GL)
        set(SIDESHOW_GL_DEFS "")
    else()
        pkg_check_modules(GLES REQUIRED IMPORTED_TARGET glesv2)
        set(SIDESHOW_GL_LIBS PkgConfig::GLES)
        set(SIDESHOW_GL_DEFS SIDESHOW_GLES)
    endif()

    add_executable(sideshow-player
        src/main.cpp
        src/display.cpp
    )
    target_compile_definitions(sideshow-player PRIVATE ${SIDESHOW_GL_DEFS})
    target_compile_options(sideshow-player PRIVATE -Wall -Wextra)
    target_link_libraries(sideshow-player PRIVATE
        sideshow_core SDL2::SDL2 Threads::Threads ${SIDESHOW_GL_LIBS})
endif()
```

- [ ] **Step 7: Build and run the player**

Run:
```bash
cmake -S . -B build -DSIDESHOW_BUILD_PLAYER=ON -DCMAKE_PREFIX_PATH="$PWD/deps/install"
cmake --build build -j
./build/sideshow-player --config setup/config.dev.toml
```
Expected: log line `[info] display 2560x1440, 4.1 Metal - …` (numbers vary; Retina doubles the size) and a dark-blue 1280×720 window. Close it with Cmd+Q or Ctrl+C in the terminal; the process exits with code 0.

- [ ] **Step 8: Confirm the unit tests still pass**

Run: `./build/sideshow_tests`
Expected: `All tests passed`

- [ ] **Step 9: Commit**

```bash
git add src/log.h src/gl.h src/display.h src/display.cpp src/main.cpp CMakeLists.txt
git commit -m "feat: open SDL/GL display in sideshow-player"
```

---

### Task 10: projectM rendering, brightness pass, and audio capture

**Files:**
- Create: `src/brightness_pass.h`, `src/brightness_pass.cpp`, `src/renderer.h`, `src/renderer.cpp`, `src/audio_capture.h`, `src/audio_capture.cpp`
- Modify: `src/main.cpp` (full replacement), `CMakeLists.txt`

- [ ] **Step 1: Create `src/brightness_pass.h`**

```cpp
#pragma once

#include "gl.h"

namespace sideshow {

// Multiplies whatever is in the default framebuffer by a brightness factor, using a
// fullscreen triangle with blend func (ZERO, SRC_ALPHA). Needs a current GL context.
class BrightnessPass {
public:
    BrightnessPass();  // throws std::runtime_error if the shaders fail to build
    ~BrightnessPass();
    BrightnessPass(const BrightnessPass&) = delete;
    BrightnessPass& operator=(const BrightnessPass&) = delete;

    void apply(float brightness, int width, int height);

private:
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLint brightnessLocation_ = -1;
};

}  // namespace sideshow
```

- [ ] **Step 2: Create `src/brightness_pass.cpp`**

```cpp
#include "brightness_pass.h"

#include <stdexcept>
#include <string>

namespace sideshow {
namespace {

#ifdef SIDESHOW_GLES
const char* kHeader = "#version 300 es\nprecision mediump float;\n";
#else
const char* kHeader = "#version 330 core\n";
#endif

// Fullscreen triangle from gl_VertexID: (-1,-1), (3,-1), (-1,3). No vertex buffers needed.
const char* kVertexBody = R"(
void main() {
    vec2 pos = vec2(float((gl_VertexID & 1) << 2) - 1.0, float((gl_VertexID & 2) << 1) - 1.0);
    gl_Position = vec4(pos, 0.0, 1.0);
}
)";

const char* kFragmentBody = R"(
uniform float u_brightness;
out vec4 fragColor;
void main() {
    fragColor = vec4(0.0, 0.0, 0.0, u_brightness);
}
)";

GLuint compileShader(GLenum type, const char* body) {
    GLuint shader = glCreateShader(type);
    const char* sources[] = {kHeader, body};
    glShaderSource(shader, 2, sources, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("brightness shader compile failed: ") + log);
    }
    return shader;
}

}  // namespace

BrightnessPass::BrightnessPass() {
    GLuint vertex = compileShader(GL_VERTEX_SHADER, kVertexBody);
    GLuint fragment = compileShader(GL_FRAGMENT_SHADER, kFragmentBody);
    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glLinkProgram(program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        glDeleteProgram(program_);
        throw std::runtime_error(std::string("brightness program link failed: ") + log);
    }
    brightnessLocation_ = glGetUniformLocation(program_, "u_brightness");
    glGenVertexArrays(1, &vao_);  // core profile / GLES3 need a bound VAO to draw
}

BrightnessPass::~BrightnessPass() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
}

void BrightnessPass::apply(float brightness, int width, int height) {
    if (brightness >= 0.999f) return;  // full brightness: nothing to do

    // Save the state we touch so projectM finds it as it left it.
    GLint prevProgram = 0, prevVao = 0;
    GLint srcRgb = 0, dstRgb = 0, srcAlpha = 0, dstAlpha = 0;
    const GLboolean blendWasOn = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_BLEND_SRC_RGB, &srcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_SRC_ALPHA);  // result = framebuffer * brightness
    glUseProgram(program_);
    glUniform1f(brightnessLocation_, brightness);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(static_cast<GLuint>(prevVao));
    glUseProgram(static_cast<GLuint>(prevProgram));
    glBlendFuncSeparate(srcRgb, dstRgb, srcAlpha, dstAlpha);
    if (!blendWasOn) glDisable(GL_BLEND);
}

}  // namespace sideshow
```

- [ ] **Step 3: Create `src/renderer.h`**

```cpp
#pragma once

#include <projectM-4/projectM.h>

#include "brightness_pass.h"
#include "config.h"

namespace sideshow {

// Owns the projectM instance. Main thread only; needs a current GL context.
class Renderer {
public:
    Renderer(int width, int height, int fps, const Config::Presets& presets);  // throws
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    projectm_handle handle() const { return projectm_; }

    void resize(int width, int height);
    void addPcm(const float* interleavedStereo, unsigned int frames);
    void render(float brightness);

    void setBeatSensitivity(float value);
    void setHardCutEnabled(bool on);
    void setPresetDuration(double seconds);

private:
    projectm_handle projectm_ = nullptr;
    BrightnessPass brightness_;
    int width_;
    int height_;
};

}  // namespace sideshow
```

- [ ] **Step 4: Create `src/renderer.cpp`**

```cpp
#include "renderer.h"

#include <stdexcept>

#include "gl.h"
#include "log.h"

namespace sideshow {

Renderer::Renderer(int width, int height, int fps, const Config::Presets& presets)
    : width_(width), height_(height) {
    projectm_ = projectm_create();
    if (!projectm_) throw std::runtime_error("projectm_create failed (no usable GL context?)");

    projectm_set_window_size(projectm_, static_cast<size_t>(width), static_cast<size_t>(height));
    projectm_set_fps(projectm_, fps);
    projectm_set_aspect_correction(projectm_, true);
    projectm_set_preset_duration(projectm_, presets.duration);
    projectm_set_soft_cut_duration(projectm_, presets.softCutDuration);
    projectm_set_hard_cut_enabled(projectm_, presets.hardcutEnabled);
    projectm_set_beat_sensitivity(projectm_, presets.beatSensitivity);

    const char* texturePaths[] = {presets.textures.c_str()};
    projectm_set_texture_search_paths(projectm_, texturePaths, 1);

    char* version = projectm_get_version_string();
    logInfo(std::string("projectM ") + version);
    projectm_free_string(version);
}

Renderer::~Renderer() {
    if (projectm_) projectm_destroy(projectm_);
}

void Renderer::resize(int width, int height) {
    width_ = width;
    height_ = height;
    projectm_set_window_size(projectm_, static_cast<size_t>(width), static_cast<size_t>(height));
}

void Renderer::addPcm(const float* interleavedStereo, unsigned int frames) {
    projectm_pcm_add_float(projectm_, interleavedStereo, frames, PROJECTM_STEREO);
}

void Renderer::render(float brightness) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    projectm_opengl_render_frame(projectm_);
    brightness_.apply(brightness, width_, height_);
}

void Renderer::setBeatSensitivity(float value) { projectm_set_beat_sensitivity(projectm_, value); }

void Renderer::setHardCutEnabled(bool on) { projectm_set_hard_cut_enabled(projectm_, on); }

void Renderer::setPresetDuration(double seconds) { projectm_set_preset_duration(projectm_, seconds); }

}  // namespace sideshow
```

- [ ] **Step 5: Create `src/audio_capture.h`**

```cpp
#pragma once

#include <SDL.h>

#include <cstdint>
#include <optional>
#include <string>

#include "audio_ring.h"

namespace sideshow {

// Captures stereo float audio from an SDL capture device into an AudioRing.
// Never fails hard: if no device is available it retries every few seconds.
class AudioCapture {
public:
    AudioCapture(std::string deviceMatch, int sampleRate, AudioRing& ring);
    ~AudioCapture();
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    void tick(uint32_t nowMs);  // opens the device when closed and a retry is due
    void handleDeviceRemoved(SDL_AudioDeviceID id);
    bool isOpen() const { return device_ != 0; }

private:
    static constexpr uint32_t kRetryMs = 3000;
    static void callback(void* userdata, Uint8* stream, int len);
    std::optional<std::string> chooseDevice() const;
    void open();
    void close();

    std::string match_;
    int sampleRate_;
    AudioRing& ring_;
    SDL_AudioDeviceID device_ = 0;
    uint32_t nextRetryMs_ = 0;
    bool warned_ = false;
};

}  // namespace sideshow
```

- [ ] **Step 6: Create `src/audio_capture.cpp`**

```cpp
#include "audio_capture.h"

#include "log.h"

namespace sideshow {

AudioCapture::AudioCapture(std::string deviceMatch, int sampleRate, AudioRing& ring)
    : match_(std::move(deviceMatch)), sampleRate_(sampleRate), ring_(ring) {}

AudioCapture::~AudioCapture() { close(); }

void AudioCapture::tick(uint32_t nowMs) {
    if (device_ != 0 || nowMs < nextRetryMs_) return;
    open();
    if (device_ == 0) nextRetryMs_ = nowMs + kRetryMs;
}

void AudioCapture::handleDeviceRemoved(SDL_AudioDeviceID id) {
    if (id != device_ || device_ == 0) return;
    logWarn("audio capture device removed; rendering silence, will retry");
    close();
    nextRetryMs_ = 0;
}

std::optional<std::string> AudioCapture::chooseDevice() const {
    const int count = SDL_GetNumAudioDevices(1);  // also re-detects devices
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetAudioDeviceName(i, 1);
        if (!name) continue;
        if (match_.empty() || std::string(name).find(match_) != std::string::npos) return std::string(name);
    }
    return std::nullopt;
}

void AudioCapture::open() {
    const auto name = chooseDevice();
    if (!name) {
        if (!warned_) {
            logWarn("no audio capture device" +
                    (match_.empty() ? std::string() : " matching '" + match_ + "'") +
                    "; rendering silence, will retry");
            warned_ = true;
        }
        return;
    }

    SDL_AudioSpec want{};
    want.freq = sampleRate_;
    want.format = AUDIO_F32SYS;
    want.channels = 2;  // SDL up-mixes a mono mic
    want.samples = 512;
    want.callback = &AudioCapture::callback;
    want.userdata = this;
    SDL_AudioSpec have{};
    device_ = SDL_OpenAudioDevice(name->c_str(), 1, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (device_ == 0) {
        if (!warned_) {
            logWarn("failed to open audio device '" + *name + "': " + SDL_GetError() + "; will retry");
            warned_ = true;
        }
        return;
    }
    warned_ = false;
    logInfo("audio capture: '" + *name + "' at " + std::to_string(have.freq) + " Hz");
    SDL_PauseAudioDevice(device_, 0);
}

void AudioCapture::close() {
    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
}

void AudioCapture::callback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<AudioCapture*>(userdata);
    self->ring_.write(reinterpret_cast<const float*>(stream), static_cast<size_t>(len) / sizeof(float));
}

}  // namespace sideshow
```

- [ ] **Step 7: Replace `src/main.cpp`** (projectM idle preset, mic input, master level at its default)

```cpp
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#include "audio_capture.h"
#include "audio_ring.h"
#include "config.h"
#include "display.h"
#include "log.h"
#include "output_level.h"
#include "renderer.h"

using namespace sideshow;

namespace {

constexpr const char* kDefaultConfigPath = "/etc/sideshow/config.toml";

std::string configPathFromArgs(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0) return argv[i + 1];
    }
    return kDefaultConfigPath;
}

}  // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();
    const std::string configPath = configPathFromArgs(argc, argv);
    ConfigResult loaded = loadConfigFile(configPath);
    for (const auto& warning : loaded.warnings) logWarn(warning);
    const Config& config = loaded.config;
    logInfo("sideshow-player starting as " + config.panelId + " (config: " + configPath + ")");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        logError(std::string("SDL_Init failed (no connected display? systemd will retry): ") +
                 SDL_GetError());
        return 1;
    }

    int exitCode = 0;
    try {
        Display display(config.display);
        Renderer renderer(display.drawableWidth(), display.drawableHeight(), config.display.fps,
                          config.presets);
        OutputLevel output(config.output.masterDefault, config.output.masterCurve,
                           config.output.masterSmoothingMs);
        AudioRing ring(static_cast<size_t>(config.audio.sampleRate / 4) * 2);  // ~250 ms stereo
        AudioCapture audio(config.audio.device, config.audio.sampleRate, ring);

        std::vector<float> pcm(projectm_pcm_get_max_samples() * 2);
        const Uint32 frameBudgetMs = 1000 / static_cast<Uint32>(std::max(1, config.display.fps));
        Uint32 lastTicks = SDL_GetTicks();
        bool running = true;

        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    running = false;
                } else if (event.type == SDL_AUDIODEVICEREMOVED && event.adevice.iscapture) {
                    audio.handleDeviceRemoved(event.adevice.which);
                } else if (event.type == SDL_WINDOWEVENT &&
                           event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    renderer.resize(display.drawableWidth(), display.drawableHeight());
                }
            }

            const Uint32 now = SDL_GetTicks();
            audio.tick(now);
            size_t n = 0;
            while ((n = ring.read(pcm.data(), pcm.size())) > 0) {
                renderer.addPcm(pcm.data(), static_cast<unsigned int>(n / 2));
            }

            output.update(static_cast<float>(now - lastTicks) / 1000.0f);
            lastTicks = now;
            renderer.render(output.brightness());
            display.swap();

            const Uint32 frameTime = SDL_GetTicks() - now;
            if (frameTime < frameBudgetMs) SDL_Delay(frameBudgetMs - frameTime);
        }
    } catch (const std::exception& e) {
        logError(e.what());
        exitCode = 1;
    }
    SDL_Quit();
    return exitCode;
}
```

- [ ] **Step 8: Update the player target in `CMakeLists.txt`**

Inside `if(SIDESHOW_BUILD_PLAYER)`, after `find_package(PkgConfig REQUIRED)` add:

```cmake
    find_package(projectM4 REQUIRED)
```

Replace the `add_executable(sideshow-player ...)` and `target_link_libraries(sideshow-player ...)` blocks with:

```cmake
    add_executable(sideshow-player
        src/main.cpp
        src/display.cpp
        src/brightness_pass.cpp
        src/renderer.cpp
        src/audio_capture.cpp
    )
```

```cmake
    target_link_libraries(sideshow-player PRIVATE
        sideshow_core SDL2::SDL2 Threads::Threads ${SIDESHOW_GL_LIBS}
        libprojectM::projectM)
```

- [ ] **Step 9: Build and run; check audio reaction and the brightness pass**

Run:
```bash
cmake -S . -B build -DSIDESHOW_BUILD_PLAYER=ON -DCMAKE_PREFIX_PATH="$PWD/deps/install"
cmake --build build -j
./build/sideshow-player --config setup/config.dev.toml
```
Expected: `[info] projectM 4.1.7`, `[info] audio capture: '<your Mac mic>' at 48000 Hz`, and projectM's idle preset (the projectM logo animation) that reacts when you clap or play music near the mic. macOS will ask to grant the terminal microphone access on first run. If it denied access earlier, enable it in System Settings → Privacy & Security → Microphone and rerun.

Then check the brightness pass: edit `setup/config.dev.toml` temporarily to `master_default = 0.5`, rerun, and confirm the output is visibly dimmer (0.5² = 25% brightness). Revert to `1.0`.

- [ ] **Step 10: Commit**

```bash
git add src/brightness_pass.h src/brightness_pass.cpp src/renderer.h src/renderer.cpp \
        src/audio_capture.h src/audio_capture.cpp src/main.cpp CMakeLists.txt
git commit -m "feat: render audio-reactive projectM with brightness pass and mic capture"
```

---

### Task 11: PresetManager (playlists)

**Files:**
- Create: `src/preset_manager.h`, `src/preset_manager.cpp`
- Modify: `src/main.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Create `src/preset_manager.h`**

```cpp
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
```

- [ ] **Step 2: Create `src/preset_manager.cpp`**

```cpp
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
```

- [ ] **Step 3: Wire it into `src/main.cpp`**

Add includes after `#include "output_level.h"`:

```cpp
#include "preset_catalog.h"
#include "preset_manager.h"
```

Replace:

```cpp
        Renderer renderer(display.drawableWidth(), display.drawableHeight(), config.display.fps,
                          config.presets);
```

with:

```cpp
        Renderer renderer(display.drawableWidth(), display.drawableHeight(), config.display.fps,
                          config.presets);
        PresetCatalog catalog(config.presets.root);
        PresetManager presets(renderer.handle(), catalog, config.presets.shuffle);
        presets.start(config.presets.playlist);
```

- [ ] **Step 4: Update the player target in `CMakeLists.txt`**

After `find_package(projectM4 REQUIRED)` add:

```cmake
    find_package(projectM4Playlist REQUIRED)
```

Add `src/preset_manager.cpp` to `add_executable(sideshow-player ...)` and `libprojectM::playlist` to its link list:

```cmake
    add_executable(sideshow-player
        src/main.cpp
        src/display.cpp
        src/brightness_pass.cpp
        src/renderer.cpp
        src/audio_capture.cpp
        src/preset_manager.cpp
    )
```

```cmake
    target_link_libraries(sideshow-player PRIVATE
        sideshow_core SDL2::SDL2 Threads::Threads ${SIDESHOW_GL_LIBS}
        libprojectM::projectM libprojectM::playlist)
```

- [ ] **Step 5: Build and run; check that presets rotate**

Run:
```bash
cmake -S . -B build -DSIDESHOW_BUILD_PLAYER=ON -DCMAKE_PREFIX_PATH="$PWD/deps/install"
cmake --build build -j
./build/sideshow-player --config setup/config.dev.toml
```
Expected: `[info] playlist 'default' (7998 presets)`, then `[info] preset: <name>.milk` about every 15 s (the dev config's `duration`), with smooth 3 s blends between audio-reactive presets.

Check the fallback too: set `playlist = "nope"` in `setup/config.dev.toml`, rerun, and confirm `[warn] playlist 'nope' missing or empty, falling back to 'default'`. Revert the change.

- [ ] **Step 6: Commit**

```bash
git add src/preset_manager.h src/preset_manager.cpp src/main.cpp CMakeLists.txt
git commit -m "feat: play preset playlists with fallback and bad-preset skipping"
```

---

### Task 12: OSC server, command dispatch, ping/pong

**Files:**
- Create: `src/osc_server.h`, `src/osc_server.cpp`
- Modify: `src/main.cpp` (full replacement), `CMakeLists.txt`

- [ ] **Step 1: Create `src/osc_server.h`**

```cpp
#pragma once

#include <lo/lo.h>

#include <mutex>
#include <string>

#include "command.h"

namespace sideshow {

// Snapshot of player state that the OSC thread reports in /sideshow/pong.
class PlayerStatus {
public:
    void set(const std::string& preset, float fps) {
        std::lock_guard<std::mutex> lock(mutex_);
        preset_ = preset;
        fps_ = fps;
    }
    void get(std::string& preset, float& fps) const {
        std::lock_guard<std::mutex> lock(mutex_);
        preset = preset_;
        fps = fps_;
    }

private:
    mutable std::mutex mutex_;
    std::string preset_;
    float fps_ = 0.0f;
};

// liblo UDP server on its own thread. Parses messages into Commands for the main loop
// and answers /sideshow/ping directly. If the port is unavailable it logs and stays idle.
class OscServer {
public:
    OscServer(int port, std::string panelId, CommandQueue& queue, const PlayerStatus& status);
    ~OscServer();
    OscServer(const OscServer&) = delete;
    OscServer& operator=(const OscServer&) = delete;

private:
    static int handle(const char* path, const char* types, lo_arg** argv, int argc,
                      lo_message message, void* userData);
    static void onError(int number, const char* message, const char* where);
    void replyPong(lo_message message);

    lo_server_thread server_ = nullptr;
    std::string panelId_;
    CommandQueue& queue_;
    const PlayerStatus& status_;
};

}  // namespace sideshow
```

- [ ] **Step 2: Create `src/osc_server.cpp`**

```cpp
#include "osc_server.h"

#include <cstring>
#include <vector>

#include "log.h"
#include "osc_parse.h"

namespace sideshow {

OscServer::OscServer(int port, std::string panelId, CommandQueue& queue, const PlayerStatus& status)
    : panelId_(std::move(panelId)), queue_(queue), status_(status) {
    const std::string portString = std::to_string(port);
    server_ = lo_server_thread_new(portString.c_str(), &OscServer::onError);
    if (!server_) {
        logError("OSC: cannot listen on UDP port " + portString + "; remote control disabled");
        return;
    }
    lo_server_thread_add_method(server_, nullptr, nullptr, &OscServer::handle, this);
    lo_server_thread_start(server_);
    logInfo("OSC: listening on UDP " + portString + " for /sideshow/all/* and /sideshow/" +
            panelId_ + "/*");
}

OscServer::~OscServer() {
    if (server_) {
        lo_server_thread_stop(server_);
        lo_server_thread_free(server_);
    }
}

int OscServer::handle(const char* path, const char* types, lo_arg** argv, int argc,
                      lo_message message, void* userData) {
    auto* self = static_cast<OscServer*>(userData);
    if (std::strcmp(path, kPingAddress) == 0) {
        self->replyPong(message);
        return 0;
    }

    std::vector<OscArg> args;
    for (int i = 0; i < argc; ++i) {
        switch (types[i]) {
            case LO_INT32: args.emplace_back(argv[i]->i); break;
            case LO_FLOAT: args.emplace_back(argv[i]->f); break;
            case LO_DOUBLE: args.emplace_back(static_cast<float>(argv[i]->d)); break;
            case LO_INT64: args.emplace_back(static_cast<int32_t>(argv[i]->h)); break;
            case LO_TRUE: args.emplace_back(int32_t{1}); break;
            case LO_FALSE: args.emplace_back(int32_t{0}); break;
            case LO_STRING: args.emplace_back(std::string(&argv[i]->s)); break;
            case LO_SYMBOL: args.emplace_back(std::string(&argv[i]->S)); break;
            default:
                logDebug(std::string("OSC: unsupported type '") + types[i] + "' in " + path);
                return 0;
        }
    }

    if (auto command = parseOscMessage(self->panelId_, path, args)) {
        self->queue_.push(std::move(*command));
    } else {
        logDebug(std::string("OSC: ignored ") + path + " ," + types);
    }
    return 0;
}

void OscServer::replyPong(lo_message message) {
    lo_address source = lo_message_get_source(message);
    std::string preset;
    float fps = 0.0f;
    status_.get(preset, fps);
    lo_send_from(source, lo_server_thread_get_server(server_), LO_TT_IMMEDIATE, kPongAddress, "ssf",
                 panelId_.c_str(), preset.c_str(), fps);
}

void OscServer::onError(int number, const char* message, const char* where) {
    logError("OSC error " + std::to_string(number) + ": " + (message ? message : "") +
             (where ? std::string(" (") + where + ")" : std::string()));
}

}  // namespace sideshow
```

- [ ] **Step 3: Replace `src/main.cpp`** (final version: presets, OSC, commands, status)

```cpp
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <cstring>
#include <exception>
#include <string>
#include <variant>
#include <vector>

#include "audio_capture.h"
#include "audio_ring.h"
#include "command.h"
#include "config.h"
#include "display.h"
#include "log.h"
#include "osc_server.h"
#include "output_level.h"
#include "preset_catalog.h"
#include "preset_manager.h"
#include "renderer.h"

using namespace sideshow;

namespace {

constexpr const char* kDefaultConfigPath = "/etc/sideshow/config.toml";

std::string configPathFromArgs(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0) return argv[i + 1];
    }
    return kDefaultConfigPath;
}

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

void applyCommand(const Command& command, PresetManager& presets, Renderer& renderer,
                  OutputLevel& output) {
    std::visit(
        Overloaded{
            [&](const cmd::Next&) { presets.next(); },
            [&](const cmd::Prev&) { presets.prev(); },
            [&](const cmd::Random&) { presets.random(); },
            [&](const cmd::Lock& c) { presets.setLocked(c.on); },
            [&](const cmd::Playlist& c) {
                if (!presets.switchPlaylist(c.name)) {
                    logWarn("OSC: unknown or empty playlist '" + c.name + "'");
                }
            },
            [&](const cmd::Preset& c) {
                if (!presets.loadPreset(c.name)) logWarn("OSC: preset not found '" + c.name + "'");
            },
            [&](const cmd::Blackout& c) { output.setBlackout(c.on); },
            [&](const cmd::Master& c) { output.setTarget(c.value); },
            [&](const cmd::Duration& c) { renderer.setPresetDuration(c.seconds); },
            [&](const cmd::BeatSensitivity& c) { renderer.setBeatSensitivity(c.value); },
            [&](const cmd::HardCut& c) { renderer.setHardCutEnabled(c.on); },
        },
        command);
}

}  // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();
    const std::string configPath = configPathFromArgs(argc, argv);
    ConfigResult loaded = loadConfigFile(configPath);
    for (const auto& warning : loaded.warnings) logWarn(warning);
    const Config& config = loaded.config;
    logInfo("sideshow-player starting as " + config.panelId + " (config: " + configPath + ")");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        logError(std::string("SDL_Init failed (no connected display? systemd will retry): ") +
                 SDL_GetError());
        return 1;
    }

    int exitCode = 0;
    try {
        // Declaration order matters: later objects are destroyed first.
        Display display(config.display);
        Renderer renderer(display.drawableWidth(), display.drawableHeight(), config.display.fps,
                          config.presets);
        PresetCatalog catalog(config.presets.root);
        PresetManager presets(renderer.handle(), catalog, config.presets.shuffle);
        presets.start(config.presets.playlist);

        OutputLevel output(config.output.masterDefault, config.output.masterCurve,
                           config.output.masterSmoothingMs);
        AudioRing ring(static_cast<size_t>(config.audio.sampleRate / 4) * 2);  // ~250 ms stereo
        AudioCapture audio(config.audio.device, config.audio.sampleRate, ring);
        CommandQueue commands;
        PlayerStatus status;
        OscServer osc(config.osc.port, config.panelId, commands, status);

        std::vector<float> pcm(projectm_pcm_get_max_samples() * 2);
        const Uint32 frameBudgetMs = 1000 / static_cast<Uint32>(std::max(1, config.display.fps));
        Uint32 lastTicks = SDL_GetTicks();
        Uint32 fpsWindowStart = lastTicks;
        int framesInWindow = 0;
        bool running = true;

        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    running = false;
                } else if (event.type == SDL_AUDIODEVICEREMOVED && event.adevice.iscapture) {
                    audio.handleDeviceRemoved(event.adevice.which);
                } else if (event.type == SDL_WINDOWEVENT &&
                           event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    renderer.resize(display.drawableWidth(), display.drawableHeight());
                }
            }

            while (auto command = commands.pop()) applyCommand(*command, presets, renderer, output);

            const Uint32 now = SDL_GetTicks();
            audio.tick(now);
            size_t n = 0;
            while ((n = ring.read(pcm.data(), pcm.size())) > 0) {
                renderer.addPcm(pcm.data(), static_cast<unsigned int>(n / 2));
            }

            output.update(static_cast<float>(now - lastTicks) / 1000.0f);
            lastTicks = now;
            renderer.render(output.brightness());
            display.swap();

            ++framesInWindow;
            if (now - fpsWindowStart >= 1000) {
                status.set(presets.currentPreset(),
                           framesInWindow * 1000.0f / static_cast<float>(now - fpsWindowStart));
                fpsWindowStart = now;
                framesInWindow = 0;
            }

            const Uint32 frameTime = SDL_GetTicks() - now;
            if (frameTime < frameBudgetMs) SDL_Delay(frameBudgetMs - frameTime);
        }
    } catch (const std::exception& e) {
        logError(e.what());
        exitCode = 1;
    }
    SDL_Quit();
    return exitCode;
}
```

- [ ] **Step 4: Update the player target in `CMakeLists.txt`**

After `find_package(projectM4Playlist REQUIRED)` add:

```cmake
    pkg_check_modules(LIBLO REQUIRED IMPORTED_TARGET liblo)
```

Update the executable sources and link list:

```cmake
    add_executable(sideshow-player
        src/main.cpp
        src/display.cpp
        src/brightness_pass.cpp
        src/renderer.cpp
        src/audio_capture.cpp
        src/preset_manager.cpp
        src/osc_server.cpp
    )
```

```cmake
    target_link_libraries(sideshow-player PRIVATE
        sideshow_core SDL2::SDL2 Threads::Threads ${SIDESHOW_GL_LIBS}
        libprojectM::projectM libprojectM::playlist PkgConfig::LIBLO)
```

- [ ] **Step 5: Build and run**

Run:
```bash
cmake -S . -B build -DSIDESHOW_BUILD_PLAYER=ON -DCMAKE_PREFIX_PATH="$PWD/deps/install"
cmake --build build -j
SIDESHOW_DEBUG=1 ./build/sideshow-player --config setup/config.dev.toml
```
Expected: `[info] OSC: listening on UDP 9000 for /sideshow/all/* and /sideshow/dev/*`. Keep it running for Task 13, which adds the CLI used to check every command.

- [ ] **Step 6: Commit**

```bash
git add src/osc_server.h src/osc_server.cpp src/main.cpp CMakeLists.txt
git commit -m "feat: accept OSC control with ping/pong status"
```

---

### Task 13: `tools/osc_send.py` + end-to-end OSC check

**Files:**
- Create: `tools/osc_send.py`, `tools/test_osc_send.py`

- [ ] **Step 1: Write the failing test `tools/test_osc_send.py`**

```python
import struct
import unittest

from osc_send import decode_message, encode_message


class EncodeTest(unittest.TestCase):
    def test_no_args_pads_address_and_type_tags(self):
        self.assertEqual(encode_message("/a", []), b"/a\0\0,\0\0\0")

    def test_four_char_string_gets_a_full_padding_word(self):
        data = encode_message("/abc", ["abcd"])
        self.assertEqual(data, b"/abc\0\0\0\0,s\0\0abcd\0\0\0\0")

    def test_int_and_float_are_big_endian(self):
        data = encode_message("/x", [7, 0.5])
        self.assertEqual(data, b"/x\0\0,if\0" + struct.pack(">i", 7) + struct.pack(">f", 0.5))

    def test_round_trip(self):
        address, args = decode_message(encode_message("/sideshow/pong", ["panel-01", "Foo.milk", 59.5]))
        self.assertEqual(address, "/sideshow/pong")
        self.assertEqual(args, ["panel-01", "Foo.milk", 59.5])

    def test_rejects_unsupported_types(self):
        with self.assertRaises(TypeError):
            encode_message("/x", [b"bytes"])


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and confirm it fails**

Run: `python3 -m unittest discover -s tools -v`
Expected: FAIL: `ModuleNotFoundError: No module named 'osc_send'`.

- [ ] **Step 3: Implement `tools/osc_send.py`**

```python
#!/usr/bin/env python3
"""Send sideshow OSC commands to one panel or the whole stage (stdlib only).

Examples:
  tools/osc_send.py next                          # every panel (broadcast)
  tools/osc_send.py --panel panel-03 blackout 1   # one panel by id
  tools/osc_send.py --host 127.0.0.1 master 0.5   # a specific host
  tools/osc_send.py ping                          # list panels that answer
"""
import argparse
import socket
import struct
import sys
import time

ARG_TYPES = {
    "next": None,
    "prev": None,
    "random": None,
    "lock": int,
    "blackout": int,
    "hardcut": int,
    "master": float,
    "duration": float,
    "beat_sensitivity": float,
    "playlist": str,
    "preset": str,
}


def _pad(data: bytes) -> bytes:
    """Null-terminate and pad to a multiple of 4 bytes (OSC string rule)."""
    return data + b"\0" * (4 - len(data) % 4)


def encode_message(address, args):
    tags = ","
    payload = b""
    for arg in args:
        if isinstance(arg, bool) or not isinstance(arg, (int, float, str)):
            raise TypeError(f"unsupported OSC argument: {arg!r}")
        if isinstance(arg, int):
            tags += "i"
            payload += struct.pack(">i", arg)
        elif isinstance(arg, float):
            tags += "f"
            payload += struct.pack(">f", arg)
        else:
            tags += "s"
            payload += _pad(arg.encode())
    return _pad(address.encode()) + _pad(tags.encode()) + payload


def _read_string(data, offset):
    end = data.index(b"\0", offset)
    text = data[offset:end].decode(errors="replace")
    return text, offset + ((end - offset) // 4 + 1) * 4


def decode_message(data):
    address, offset = _read_string(data, 0)
    tags, offset = _read_string(data, offset)
    args = []
    for tag in tags[1:]:
        if tag == "i":
            args.append(struct.unpack_from(">i", data, offset)[0])
            offset += 4
        elif tag == "f":
            args.append(struct.unpack_from(">f", data, offset)[0])
            offset += 4
        elif tag == "s":
            text, offset = _read_string(data, offset)
            args.append(text)
        else:
            raise ValueError(f"unsupported OSC type tag {tag!r}")
    return address, args


def ping(sock, host, port, wait_seconds=1.0):
    sock.sendto(encode_message("/sideshow/ping", []), (host, port))
    sock.settimeout(0.2)
    deadline = time.monotonic() + wait_seconds
    while time.monotonic() < deadline:
        try:
            data, addr = sock.recvfrom(4096)
        except socket.timeout:
            continue
        address, args = decode_message(data)
        if address == "/sideshow/pong" and len(args) == 3:
            panel, preset, fps = args
            print(f"{addr[0]:15}  {panel:12}  {fps:5.1f} fps  {preset}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="255.255.255.255", help="panel IP or broadcast address")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--panel", default="all", help="panel id, or 'all' (default)")
    parser.add_argument("command", choices=sorted(ARG_TYPES) + ["ping"])
    parser.add_argument("value", nargs="?")
    options = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    if options.command == "ping":
        ping(sock, options.host, options.port)
        return 0

    arg_type = ARG_TYPES[options.command]
    if arg_type is None and options.value is not None:
        parser.error(f"{options.command} takes no value")
    if arg_type is not None and options.value is None:
        parser.error(f"{options.command} needs a value")
    args = [] if arg_type is None else [arg_type(options.value)]
    address = f"/sideshow/{options.panel}/{options.command}"
    sock.sendto(encode_message(address, args), (options.host, options.port))
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the tests**

Run: `chmod +x tools/osc_send.py && python3 -m unittest discover -s tools -v`
Expected: `Ran 5 tests … OK`

- [ ] **Step 5: End-to-end check against the running player (from Task 12 Step 5)**

With `SIDESHOW_DEBUG=1 ./build/sideshow-player --config setup/config.dev.toml` running, send each command in turn and watch the window and the log:

```bash
H="--host 127.0.0.1"
tools/osc_send.py $H ping                          # prints: 127.0.0.1  dev  ~60.0 fps  <preset>.milk
tools/osc_send.py $H next                          # blends to a new preset; "[info] preset: …"
tools/osc_send.py $H prev
tools/osc_send.py $H random
tools/osc_send.py $H lock 1                        # "[info] preset lock on"; no auto-advance after 15 s
tools/osc_send.py $H lock 0
tools/osc_send.py $H playlist fractal              # "[info] playlist 'fractal' (… presets)"
tools/osc_send.py $H playlist nope                 # "[warn] OSC: unknown or empty playlist 'nope'"
tools/osc_send.py $H preset "$(basename "$(ls deps/presets/fractal/*/*.milk | head -1)" .milk)"
tools/osc_send.py $H master 0.3                    # fades to dim (~9%) over ~50 ms
tools/osc_send.py $H master 1.0
tools/osc_send.py $H blackout 1                    # instantly black
tools/osc_send.py $H master 0.5                    # still black (blackout wins)
tools/osc_send.py $H blackout 0                    # back, at 25% brightness
tools/osc_send.py $H master 1.0
tools/osc_send.py $H duration 5                    # presets now change every ~5 s
tools/osc_send.py $H hardcut 1                     # abrupt beat-synced cuts on loud music
tools/osc_send.py $H hardcut 0
tools/osc_send.py $H beat_sensitivity 1.5
tools/osc_send.py $H --panel dev next              # own panel id: works
tools/osc_send.py $H --panel panel-99 next         # other panel: nothing happens; debug log "OSC: ignored"
```

Expected: every line behaves as its comment says, and the player never exits.

- [ ] **Step 6: Commit**

```bash
git add tools/osc_send.py tools/test_osc_send.py
git commit -m "feat: add dependency-free OSC CLI for testing and soundcheck"
```

---

### Task 14: Raspberry Pi provisioning (config template, systemd unit, install script)

**Files:**
- Create: `setup/config.example.toml`, `setup/sideshow-player.service`, `setup/install.sh`

- [ ] **Step 1: Create `setup/config.example.toml`**

```toml
# /etc/sideshow/config.toml: sideshow-player configuration.
# Missing or invalid keys fall back to the defaults shown here (a warning is logged).

panel_id = "panel-01"          # [a-z0-9-]; set by install.sh --panel-id

[display]
width = 0                      # 0 = automatic: the screen's native mode, capped below
height = 0                     # set both (e.g. 1920 x 1080) to force a mode
max_width = 2560               # automatic mode never exceeds this (4K screens step down)
max_height = 1440
fps = 60
fullscreen = true

[audio]
device = ""                    # "" = first capture device; else part of its name, e.g. "USB"
sample_rate = 48000

[osc]
port = 9000

[presets]
root = "/opt/sideshow/presets" # each subdirectory is a playlist
textures = "/opt/sideshow/textures"
playlist = "default"
duration = 30.0                # seconds per preset
soft_cut_duration = 3.0        # blend time between presets
hardcut_enabled = false        # beat-triggered abrupt cuts
beat_sensitivity = 1.0         # 0..2
shuffle = true

[output]
master_default = 1.0           # brightness at boot, before any OSC master arrives
master_curve = 2.0             # 1.0 = linear; 2.0 = perceptually even fade
master_smoothing_ms = 50
```

- [ ] **Step 2: Create `setup/sideshow-player.service`**

```ini
[Unit]
Description=Sideshow audio-reactive visuals player
After=sound.target
Wants=sound.target
# Keep restarting forever; never give up mid-show.
StartLimitIntervalSec=0

[Service]
Type=simple
User=sideshow
Group=sideshow
SupplementaryGroups=video audio render input
Environment=SDL_VIDEODRIVER=kmsdrm
Environment=SDL_AUDIODRIVER=alsa
ExecStart=/opt/sideshow/bin/sideshow-player --config /etc/sideshow/config.toml
Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target
```

- [ ] **Step 3: Create `setup/install.sh`**

```bash
#!/usr/bin/env bash
# Provision a Raspberry Pi 5 (Raspberry Pi OS Lite, Bookworm 64-bit) as a sideshow panel.
# Safe to re-run. Usage: sudo setup/install.sh [--panel-id panel-03]
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
INSTALL_ROOT=/opt/sideshow
CONFIG=/etc/sideshow/config.toml
CMDLINE=/boot/firmware/cmdline.txt
BOOTCONFIG=/boot/firmware/config.txt
PANEL_ID=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --panel-id) PANEL_ID="${2:?--panel-id needs a value}"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [[ $EUID -ne 0 ]]; then
    echo "run with sudo" >&2
    exit 1
fi
if [[ -n "$PANEL_ID" && ! "$PANEL_ID" =~ ^[a-z0-9][a-z0-9-]{0,31}$ ]]; then
    echo "panel id must be lowercase letters, digits, and dashes (e.g. panel-03)" >&2
    exit 2
fi

echo "==> Installing packages"
apt-get update
apt-get install -y build-essential cmake git pkg-config \
    libsdl2-dev liblo-dev libgles-dev libegl-dev

echo "==> Building libprojectM"
"$REPO_DIR/setup/build_projectm.sh" /usr/local --gles
ldconfig

echo "==> Building sideshow-player"
BUILD_DIR="$INSTALL_ROOT/build"
cmake -S "$REPO_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DSIDESHOW_BUILD_PLAYER=ON -DSIDESHOW_BUILD_TESTS=OFF
cmake --build "$BUILD_DIR" --parallel "$(nproc)"
install -D -m 0755 "$BUILD_DIR/sideshow-player" "$INSTALL_ROOT/bin/sideshow-player"

echo "==> Fetching presets and textures"
"$REPO_DIR/setup/fetch_presets.sh" "$INSTALL_ROOT"

echo "==> Writing config"
install -d /etc/sideshow
if [[ ! -f "$CONFIG" ]]; then
    install -m 0644 "$REPO_DIR/setup/config.example.toml" "$CONFIG"
fi
if [[ -n "$PANEL_ID" ]]; then
    sed -i "s/^panel_id = .*/panel_id = \"$PANEL_ID\"          # set by install.sh --panel-id/" "$CONFIG"
    hostnamectl set-hostname "$PANEL_ID"
    if grep -q '^127\.0\.1\.1' /etc/hosts; then
        sed -i "s/^127\.0\.1\.1.*/127.0.1.1\t$PANEL_ID/" /etc/hosts
    else
        printf '127.0.1.1\t%s\n' "$PANEL_ID" >> /etc/hosts
    fi
fi

echo "==> Creating service user"
if ! id sideshow &>/dev/null; then
    useradd --system --home-dir /var/lib/sideshow --create-home --shell /usr/sbin/nologin sideshow
fi
usermod -aG video,audio,render,input sideshow

echo "==> Installing systemd service"
install -m 0644 "$REPO_DIR/setup/sideshow-player.service" /etc/systemd/system/sideshow-player.service
systemctl daemon-reload
systemctl enable sideshow-player.service
systemctl disable getty@tty1.service || true   # no login prompt flashing on the panel

echo "==> Quieting boot"
sed -i 's/console=tty1/console=tty3/' "$CMDLINE"
for option in quiet logo.nologo vt.global_cursor_default=0 consoleblank=0; do
    grep -qw -- "$option" "$CMDLINE" || sed -i "1 s|\$| $option|" "$CMDLINE"
done
grep -q '^disable_splash=1' "$BOOTCONFIG" || echo 'disable_splash=1' >> "$BOOTCONFIG"

echo
echo "Done. Reboot to start visuals:  sudo reboot"
echo "Logs:                           journalctl -u sideshow-player -f"
```

- [ ] **Step 4: Lint the scripts locally**

Run: `chmod +x setup/install.sh && bash -n setup/install.sh setup/build_projectm.sh setup/fetch_presets.sh setup/dev-macos.sh && echo syntax-ok`
Expected: `syntax-ok`. If `shellcheck` is installed (`brew install shellcheck`), also run `shellcheck setup/*.sh`. Expected: no errors (style notes are fine).

- [ ] **Step 5: Commit**

```bash
git add setup/config.example.toml setup/sideshow-player.service setup/install.sh
git commit -m "build: add Pi provisioning script, systemd unit, and config template"
```

---

### Task 15: README

**Files:**
- Create: `README.md`

- [ ] **Step 1: Create `README.md`**

````markdown
# Sideshow

Autonomous, audio-reactive stage visuals for 6–8 panels, each driven by a Raspberry Pi 5
running [projectM](https://github.com/projectM-visualizer/projectm) (MilkDrop presets).
Each Pi listens to its own USB mic, boots straight into visuals, and takes live control over OSC.

## Raspberry Pi setup (per panel)

1. Flash **Raspberry Pi OS Lite (64-bit, Bookworm)** with Raspberry Pi Imager. In its settings, set
   the username/password, enable SSH, and enter the stage Wi-Fi.
2. Plug in the USB mic and HDMI, boot, then SSH in:
   ```bash
   sudo apt-get install -y git
   git clone <this repo> ~/sideshow
   sudo ~/sideshow/setup/install.sh --panel-id panel-01
   sudo reboot
   ```
   Use a unique id per panel (`panel-01` … `panel-08`); it becomes the hostname
   (`ssh <user>@panel-01.local`) and the panel's OSC address.
3. After reboot the panel goes black → visuals in about 20 s. No keyboard, mouse, or network needed.

To update: `cd ~/sideshow && git pull && sudo setup/install.sh` (keeps your `/etc/sideshow/config.toml`).

Useful commands:

```bash
journalctl -u sideshow-player -f          # live logs
sudo systemctl restart sideshow-player    # restart the player
sudo nano /etc/sideshow/config.toml       # settings (restart to apply)
```

Notes:
- Resolution is automatic: the player uses the screen's native mode, capped at 2560×1440
  (a 4K screen steps down to 1440p or 1080p), preferring 60 Hz. The chosen mode is logged
  (`screen native …, using …`). Change `max_width`/`max_height`, or set `width`/`height`, in
  the config to override.
- If no screen is detected at boot, the player exits and systemd retries every 2 s, so visuals
  appear shortly after the screen is powered on.
- The tty1 login prompt is disabled; use SSH.

## OSC control

UDP port 9000. Address one panel as `/sideshow/<panel-id>/<command>` or all panels as
`/sideshow/all/<command>`. Send to a panel's IP, or to the subnet broadcast address (e.g.
`192.168.1.255`) to reach every panel with one message.

| Command | Args | Effect |
|---|---|---|
| `next` / `prev` / `random` | – | Change preset (soft blend) |
| `lock` | int 0/1 | Freeze the current preset |
| `playlist` | string | Switch playlist (e.g. `default`, `fractal`, `hypnotic`) |
| `preset` | string | Load a preset by filename (`.milk` optional) |
| `blackout` | int 0/1 | Instant black (overrides master) |
| `master` | float 0–1 | Output brightness (smoothed, curved) |
| `duration` | float s | Seconds per preset (1–3600) |
| `beat_sensitivity` | float | 0–2 |
| `hardcut` | int 0/1 | Beat-triggered hard cuts |

`/sideshow/ping` → every panel replies `/sideshow/pong <panel-id> <preset> <fps>` to the sender.
Ints and floats are interchangeable for toggles and numbers.

CLI (no dependencies):

```bash
tools/osc_send.py ping                          # list panels on the network
tools/osc_send.py next                          # all panels (broadcast)
tools/osc_send.py --panel panel-03 blackout 1   # one panel
tools/osc_send.py --host 192.168.1.42 master 0.5
```

### VDMX master fader

In VDMX, add an OSC output to the subnet broadcast address (e.g. `192.168.1.255`), port `9000`.
Map the fader (the one your MIDI fader controls) to the address `/sideshow/all/master`, with the
output range 0.0–1.0 as a float. If VDMX won't send to a broadcast address, add one output per
panel IP with the same OSC address.

The panels apply `master_curve = 2.0` so the fade looks even. If you shape the curve in VDMX
instead, set `master_curve = 1.0` in each panel's config.

## Playlists

Each directory (or symlink) under `/opt/sideshow/presets` is a playlist. The install creates
`default` (the full projectM-presets-rpi5 pack, 7,998 presets) and one per sub-pack/category
(`dancer`, `drawing`, `fractal`, `geometric`, `hypnotic`, `particles`, `reaction`, `sparkle`,
`supernova`, `waveform`, `isosceles`, `bltc`, `en-d`). To make a curated playlist, create a
directory and symlink or copy `.milk` files into it, then restart the player.

## Development (macOS)

```bash
setup/dev-macos.sh                                  # one time: brew deps, projectM, presets
cmake -S . -B build -DSIDESHOW_BUILD_PLAYER=ON -DCMAKE_PREFIX_PATH="$PWD/deps/install"
cmake --build build -j
./build/sideshow_tests                              # unit tests
python3 -m unittest discover -s tools               # OSC CLI tests
./build/sideshow-player --config setup/config.dev.toml
tools/osc_send.py --host 127.0.0.1 ping
```

Set `SIDESHOW_DEBUG=1` to log ignored OSC messages.

## On-hardware smoke checklist

Run this on each new panel, and after any significant change:

- [ ] Power on with no keyboard or network: visuals appear within ~20 s, with no boot text or cursor
- [ ] Visuals react to clapping and music near the mic
- [ ] `tools/osc_send.py ping` lists the panel at ~60 fps
- [ ] Every OSC command in the table works, both via `--panel <id>` and via broadcast `all`
- [ ] The VDMX fader fades smoothly to black and back, with no visible stepping
- [ ] Unplug the mic: visuals keep running (on silence). Replug: reaction returns within ~3 s
- [ ] `sudo pkill -9 sideshow-player`: visuals return within ~3 s
- [ ] Pull the power and restore it: visuals come back on their own
- [ ] Boot with the screen off, then turn it on: visuals show within a few seconds, at the right resolution
- [ ] The log line `screen native …, using …` shows the expected mode
````

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: add README with Pi setup, OSC reference, VDMX, and smoke checklist"
```

---

### Task 16: First Pi bring-up (manual, on hardware)

This needs a Pi 5 with a USB mic and a screen. Do it with the user.

- [ ] **Step 1: Push the repo somewhere the Pi can clone it** (ask the user where: a GitHub repo, or `scp -r` the working tree to the Pi).

- [ ] **Step 2: Follow README "Raspberry Pi setup" with `--panel-id panel-01`.** Expected: `install.sh` finishes with "Done. Reboot to start visuals". The projectM build takes about 5–10 minutes on a Pi 5.

- [ ] **Step 3: Reboot and watch the logs**

Run (over SSH): `journalctl -u sideshow-player -f`
Expected: `screen native <W>x<H>@60Hz, using <W>x<H>@60Hz`, `display <W>x<H>, OpenGL ES 3.1 …`, `projectM 4.1.7`, `audio capture: '<USB mic name>' …`, `playlist 'default' (7998 presets)`, `OSC: listening on UDP 9000 …`.

- [ ] **Step 4: Run the README smoke checklist.** Record each item's result and fps in the commit message.

- [ ] **Step 5: If fps is below ~55,** note the chosen mode from the log, then try lowering the cap (`max_width = 1920`, `max_height = 1080`) in `/etc/sideshow/config.toml` and restart. A scaled render mode (render below output resolution) is agreed follow-up work if the cap alone isn't enough. Then note any presets that are consistently slow, as candidates for a curated `stage` playlist. Report the findings to the user before changing code.

- [ ] **Step 6: Commit any fixes found during bring-up** with messages describing the hardware behavior they address.
