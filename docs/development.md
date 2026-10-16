# Development

Run commands from the repository root. The project requires a C++17 compiler, CMake 3.20+
and Git. Python 3 runs the OSC tool and its tests without additional packages.

## Core build and tests

The default build excludes the player and needs no GPU, microphone, SDL, or projectM.
CMake fetches toml++ v3.4.0 and, when tests are enabled, Catch2 v3.7.1 from GitHub, so a
first configuration needs network access.

```bash
cmake -S . -B build-core -DSIDESHOW_BUILD_PLAYER=OFF -DSIDESHOW_BUILD_TESTS=ON
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
python3 -m unittest discover -s tools
```

Use a separate build directory to keep core-only and full-player CMake settings independent.
Generated `build/`, `build-*/`, and `deps/` directories are ignored by Git.

## macOS player

Install the Xcode command-line tools and Homebrew first. The setup script installs CMake,
pkg-config, SDL2, liblo, and FFmpeg through Homebrew, builds projectM v4.1.7 with its playlist
library into `deps/install`, and downloads pinned preset/texture packs into `deps`.

```bash
setup/dev-macos.sh
cmake -S . -B build -DSIDESHOW_BUILD_PLAYER=ON -DCMAKE_PREFIX_PATH="$PWD/deps/install"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/sideshow-player --config setup/config.dev.toml
```

The development config opens a 1280×720 window with panel ID `dev` and 15-second presets.
Its asset paths are relative to the working directory, so launch from the repository root.
Allow microphone access if macOS requests it. In another terminal:

```bash
python3 tools/osc_send.py --host 127.0.0.1 ping
python3 tools/osc_send.py --host 127.0.0.1 --panel dev next
```

Prefix the player command with `SIDESHOW_DEBUG=1` to inspect ignored OSC traffic.
Close the window or use Ctrl+C to stop it.

## Raspberry Pi deployment

Follow the [per-panel setup](../README.md#raspberry-pi-setup-per-panel) on Raspberry Pi OS
Lite Bookworm, 64-bit. `setup/install.sh` is a machine-provisioning script: it installs
packages, builds projectM with GLES into `/usr/local`, builds the player with tests disabled,
installs assets, configures a service account/systemd, and adjusts boot settings and tty1.
Passing `--panel-id` also changes the hostname and configured panel ID.

| Installed path | Purpose |
|---|---|
| `/opt/sideshow/bin/sideshow-player` | Player executable |
| `/opt/sideshow/build` | Player build directory |
| `/opt/sideshow/presets` | Playlist directories/symlinks |
| `/opt/sideshow/textures` | Merged texture files |
| `/opt/sideshow/packs` | Pinned upstream asset checkouts |
| `/etc/sideshow/config.toml` | Persistent local configuration |
| `/etc/systemd/system/sideshow-player.service` | Boot/restart service |

Reinstallation preserves an existing config except an explicitly supplied panel ID.
Asset fetching rebuilds managed textures and playlist links; keep custom playlists in
separately named directories under `presets`, and do not edit managed pack checkouts.

## Making changes

Keep independently testable behavior in `sideshow_core`; keep SDL/liblo/projectM integration
in the player target. Preserve the [thread ownership rules](architecture.md#runtime-and-ownership).
Add new compiled source/test files explicitly to `CMakeLists.txt`.

For a new OSC command, trace `command.h` → `osc_parse.cpp` → `main.cpp` dispatch and update
the Python CLI and README command table as appropriate. For configuration changes, keep
`config.h`, `config.cpp`, example configs, and the [reference](configuration.md) aligned.

The C++ suite covers configuration, output levels, audio buffering and silence fallback,
bounded command processing, OSC parsing, preset catalog operations, and display mode selection.
A separate `sideshow_preset_tests` target compiles the real preset manager against a test-only
projectM API double to exercise failure callbacks, cleanup, navigation, and fallback without
a GPU or projectM installation. Python tests cover OSC encoding/decoding and CLI value parsing.
These tests do not exercise GL rendering, real audio devices, liblo networking, the real
projectM callback integration, or systemd recovery. Use the README's
hardware smoke checklist for integration changes; a passing unit suite is not a stage
performance measurement.

Dependency versions live in `CMakeLists.txt`, `setup/build_projectm.sh`, and
`setup/fetch_presets.sh`. The earlier [design](superpowers/specs/2026-09-22-sideshow-player-design.md)
and [plan](superpowers/plans/2026-09-22-sideshow-player.md) provide historical rationale.

## Fluid rendering validation

The core suite also covers fluid configuration/catalog, navigation and timers, audio/ambient bounds, and transactional mode switching with a GPU-boundary test backend. Optional real-context tests compile shaders, render all eleven presets, exercise conservative RGBA/manual-filter capabilities and resize failures, and switch real Fluid/projectM backends under blackout.

```bash
cmake -S . -B build -DSIDESHOW_BUILD_PLAYER=ON -DSIDESHOW_BUILD_GL_TESTS=ON \
  -DCMAKE_PREFIX_PATH="$PWD/deps/install"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/sideshow_gl_tests
python3 -m unittest discover -s tools
```

GL tests are deliberately separate from default CTest and require a working SDL GL context. A missing context is a failed prerequisite, never a passing GPU test. `SIDESHOW_BUILD_GL_TESTS` requires `SIDESHOW_BUILD_PLAYER=ON`.

Validated on the development Mac with desktop GL: shader compilation, all eleven presets, wider float target/manual advection fallback, repeated real backend switches, blackout, resizing, and failed candidate retention. Core tests also run under ASan/UBSan. Raspberry Pi 5 GLES, microphone hardware response, sustained performance/thermals, and systemd recovery remain unverified; use the README hardware checklist before deploying to a show.

On 2026-10-16 the development Mac (arm64, OpenGL 4.1 Metal 90.5) reported 59.1–60.1 FPS after warm-up at a 1280×720 drawable with default simulation/dye quality. This short desktop sample is not a Pi benchmark or sustained thermal test. Live OSC switching restored `fluid/lava` and the previous MilkDrop preset. A projectM texture warning reproduced with the unchanged baseline player as well.

The GL test executable uses test-only wrappers around real GL allocation/deletion calls to verify resource cleanup after injected shader failure and failed candidate initialization. Production builds do not include those wrappers.

## Video mode

See [local video playback](video.md) for OSC controls, recursive folder semantics,
configuration, FFmpeg dependencies, architecture limits and hardware validation.

Video validation on the development Mac: full player build; 96 C++ cases (including
real FFmpeg and worker tests); 11 Python cases; Fluid and Video real GL executables;
core/decoder AddressSanitizer and UndefinedBehaviorSanitizer; fresh core-only build.
Local OSC tested nested paths with spaces, pause/restart, loop/hold/next, lock,
blackout, and return to both other modes. A synthetic 64×48 H.264 clip at a 2560×1440
Retina drawable ran at approximately 59 FPS. This is a small desktop smoke test,
not evidence of 1080p decoding throughput or Raspberry Pi performance. Development
libraries were libavformat/libavcodec 63.1.102, libavutil 61.1.102, libswscale 10.1.102.
Actual FFmpeg 5.1 compilation and Pi/GLES hardware checks remain outstanding.

## ISF validation

Enable `SIDESHOW_BUILD_ISF_TESTS=ON` for the real-context, runtime and backend
executables: `sideshow_isf_context_tests`, `sideshow_isf_runtime_tests` and
`sideshow_isf_backend_tests`. These need a real display/GL context. Pure metadata,
file, audio, parameter, loader, playback and budget tests remain in `sideshow_tests`.
Playback tests cover startup fallback, stale generations, rejected selections,
queue capacity/order, shared validation, direct-folder navigation and timing without GL.
VVISF is pinned in `cmake/IsfSource.cmake`; local patches are applied by
`cmake/IsfDependencies.cmake`. Core-only builds consume its JSON header without
linking the GL library. Linux uses modern EGL/GLES3, without Qt, GLFW or legacy
Broadcom SDK requirements.

Run `python3 tools/test_isf_osc_integration.py --run-live` for an opt-in windowed
smoke test through the real OSC CLI. It uses a temporary library/config and local
UDP port, terminates only its own player, and reports desktop FPS and RSS. Normal
Python test discovery does not launch it. Pixel assertions live in GL tests;
pong only confirms selection and liveness. See [validation evidence and pending
hardware checks](superpowers/isf-validation.md).
