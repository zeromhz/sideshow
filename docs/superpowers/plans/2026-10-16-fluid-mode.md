# Selectable Fluid Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an autonomous, audio-reactive native fluid mode alongside MilkDrop, with eleven Oilwheel-inspired presets and existing stage controls.

**Architecture:** A GPU-independent visualization controller routes commands to one active backend and commits a mode switch only after its replacement initializes successfully. The existing projectM renderer becomes the MilkDrop backend; a native fluid backend combines independently tested playback/motion logic with a port of the upstream GPU solver. One shared output pass applies brightness and blackout after either backend.

**Tech Stack:** C++17, CMake 3.20+, SDL2, OpenGL ES 3 / desktop OpenGL 3.3, existing projectM 4.1.7, liblo, toml++, Catch2, standard-library Python.

**Spec:** `docs/superpowers/specs/2026-10-16-fluid-mode-design.md` (approved by the user).

## Global Constraints

- Each panel renders one mode at a time; no simultaneous layering or mixing.
- No Chromium, desktop, internet access, or second microphone capture session at runtime.
- MilkDrop remains the startup default for existing configurations.
- Mode switches are frame-boundary cuts, not crossfades.
- Preserve master/blackout and shared duration, lock, and beat-sensitivity settings across switches.
- Remember per-mode selected preset and playlist for the process lifetime; returning restarts the look.
- Release inactive GPU resources after successfully preparing their replacement.
- Keep existing OSC addresses and `/sideshow/pong`'s `string, string, float` signature.
- Fluid defaults: simulation resolution 128, dye resolution 512, ambient interval 2.5 s, preset `light-show`.
- Simulation resolution: integer 32–256; dye resolution: integer 128–1024; ambient interval: 0.1–60 s.
- At most three audio-triggered splashes and one ambient splash per frame; 120 ms audio cooldown.
- Solver timestep capped at 1/60 s; never perform unbounded catch-up work.
- Main thread alone accesses GPU resources or visualization state. Preserve bounded OSC/audio processing.
- Keep default core-only builds free of GPU, SDL, and projectM development dependencies.
- Include upstream MIT notice, pinned revision, source URLs, and Oilwheel attribution; do not import website UI or analytics.
- Report Pi/GLES performance as unverified until tested on hardware.

## Review Focus

1. Fluid allocation/shader failure midway through a switch must retain the working backend and clean up candidate resources (Tasks 3–5).
2. Silence, NaN/Inf PCM, device loss, and long frame gaps must not produce unlimited or persistent audio bursts (Task 2).
3. Rapid mode switches while blacked out must not flash a bright frame or leak GL objects (Tasks 3, 5, 6).
4. Selecting Overhead or a custom-color preset and then another preset must not inherit inversion, palette, or effect flags (Tasks 1, 2, 6).
5. Zero-size resize events and missing float texture/filter capabilities must not trigger invalid allocation, division by zero, or a restart loop (Tasks 4–6).

## Execution and baseline

Use the approved execution method at handoff. Before product edits, apply the worktree skill's existing-workspace checks and honor the user's workspace preference. Do not create an unrequested remote branch or push feature work. Keep implementation commits separate from the already committed spec.

Run the baseline before editing:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python3 -m unittest discover -s tools
git status --short
```

The current baseline has 65 C++ cases and 9 Python cases. In a fresh checkout, configure a core-only build first; use `setup/dev-macos.sh` only if player dependencies are actually missing. Do not rerun provisioning on this development Mac.

## File and interface map

| File | Responsibility |
|---|---|
| `src/visual_mode.h`, `src/visual_mode.cpp` | Mode enum, parsing, stable names |
| `src/fluid_presets.h`, `src/fluid_presets.cpp` | Complete immutable preset data and lookup |
| `src/fluid_playback.h`, `src/fluid_playback.cpp` | Selection, lock, rotation, shuffle, and palette state |
| `src/fluid_motion.h`, `src/fluid_motion.cpp` | PCM energy/onset tracking and bounded seeded splashes |
| `src/visual_backend.h` | GPU-independent backend contract and initialization/settings records |
| `src/visual_controller.h`, `src/visual_controller.cpp` | Transactional switching, command routing, saved selections |
| `src/milkdrop_backend.h`, `src/milkdrop_backend.cpp` | Own existing Renderer/Catalog/PresetManager in safe lifetime order |
| `src/fluid_gl.h`, `src/fluid_gl.cpp` | Fluid-local movable GL resource owners and format probes |
| `src/fluid_shaders.h`, `src/fluid_shaders.cpp` | Attributed upstream shaders adapted to GL/GLES |
| `src/fluid_renderer.h`, `src/fluid_renderer.cpp` | Solver, framebuffer resizing, bloom, sunrays, display |
| `src/fluid_backend.h`, `src/fluid_backend.cpp` | Compose fluid playback, motion, and renderer |
| `third_party/fluid/README.md`, `third_party/fluid/LICENSE` | Revision, hashes, sources, adaptation notes, license |
| Existing config/command/CLI/main/output files | Integrate mode without changing existing wire formats |
| `tests/test_fluid_*.cpp`, `tests/test_visual_controller.cpp` | GPU-independent behavior tests |
| `tests/test_visual_gl.cpp` | Optional real-context integration executable |

Do not grow `main.cpp` into an engine implementation. Do not build a general plugin framework. GL helpers are local to fluid, not a repository-wide graphics rewrite.

## Task 1: Preset catalog, mode command, and configuration

**Files:** create `src/visual_mode.{h,cpp}`, `src/fluid_presets.{h,cpp}`, `tests/test_fluid_presets.cpp`, `third_party/fluid/{README.md,LICENSE}`; modify `src/{config.h,config.cpp,command.h,osc_parse.cpp}`, `tests/{test_config.cpp,test_osc_parse.cpp}`, `tools/{osc_send.py,test_osc_send.py}`, `CMakeLists.txt`, `setup/config.example.toml`, `setup/config.dev.toml`.

**Interfaces:**

```cpp
enum class VisualMode { Milkdrop, Fluid };
std::optional<VisualMode> parseVisualMode(std::string_view value);
std::string_view visualModeName(VisualMode mode);
struct Color { float r, g, b; }; // normalized RGB palette values, finite 0..1
enum class FluidPalette { Spectrum, Warm, Mono, Custom };
struct FluidPreset {
    std::string_view slug, name;
    FluidPalette palette;
    std::vector<Color> colors;
    float monoHue, colorUpdateSpeed, curl, splatForce, splatRadius;
    float velocityDissipation, densityDissipation;
    float pressure;
    int pressureIterations;
    bool colorful, shading, bloom, sunrays, ink;
    float bloomIntensity, bloomThreshold, bloomSoftKnee, sunraysWeight;
};
const std::vector<FluidPreset>& fluidPresets();
const FluidPreset* findFluidPreset(std::string_view slugOrName);
// Add cmd::Mode { VisualMode value; } to Command.
// Add Config::Visuals { VisualMode mode = VisualMode::Milkdrop; } visuals;
// Add Config::Fluid with preset, simulationResolution, dyeResolution, ambientInterval.
```

- [ ] Write failing config/parser/catalog tests. Check all eleven slugs in spec order, case-insensitive display names, and unknown lookup. Use literal values for representative defaults and boundary rejection; include wrong types, NaN/Inf, and out-of-range resolutions. Require each preset's palette/effect values independently of selection history.

```cpp
TEST_CASE("Fluid presets do not inherit ink or palette settings") {
    REQUIRE(findFluidPreset("Overhead")->ink);
    CHECK_FALSE(findFluidPreset("light-show")->ink);
    CHECK(findFluidPreset("Light Show")->palette == FluidPalette::Spectrum);
    CHECK(findFluidPreset("unknown") == nullptr);
}
TEST_CASE("OSC mode accepts only supported engine names") {
    REQUIRE(parseOscMessage("panel-01", "/sideshow/all/mode", {std::string("fluid")}));
    CHECK_FALSE(parseOscMessage("panel-01", "/sideshow/all/mode", {std::string("browser")}));
}
```

- [ ] Run new tests and observe the missing behavior before implementing it. Add the new source/test files to `sideshow_core`/`sideshow_tests`; preserve the player-off build.
- [ ] Fetch the two public references for inspection only. Resolve the upstream repository's current revision, fetch `script.js` and `LICENSE` at that exact revision, and record SHA-256 hashes. Record a hash/date for the Oilwheel page. Retain upstream license text and source/adaptation notes, without copying the surrounding website.
- [ ] Expand every Oilwheel preset over the upstream defaults into a complete `FluidPreset` initializer. Copy the eleven numeric settings/palettes from the page's `const PRESETS` table; use upstream defaults for absent fields, including pressure 0.8 and 20 iterations. Normalize hex colors by division by 255. Implement ASCII case-insensitive slug/display-name lookup without accepting arbitrary filesystem paths.
- [ ] Add `[visuals]` and `[fluid]` parsing using existing per-key fallback/warnings. Add `mode` as a string CLI command; reject unknown mode arguments in the player. Add a Python test asserting `mode fluid` produces the correct OSC address and string argument.
- [ ] Add a temporary explicit command-dispatch branch logging that the requested mode is not yet available, so this intermediate commit still builds without claiming fluid support. Task 5 replaces it with controller dispatch.
- [ ] Run C++/Python suites and full player build, then commit this tested catalog/protocol/configuration increment.

## Task 2: GPU-independent fluid playback and motion

**Files:** create `src/fluid_playback.{h,cpp}`, `src/fluid_motion.{h,cpp}`, `tests/test_fluid_playback.cpp`, `tests/test_fluid_motion.cpp`; modify `CMakeLists.txt`.

**Consumes:** `FluidPreset`, `Color`, `findFluidPreset()`, `Config::Fluid`, seeded `std::mt19937`.
**Produces:**

```cpp
struct PlaybackSettings {
    double duration = 30.0;
    bool shuffle = true, locked = false, hardcut = false;
    float beatSensitivity = 1.0f;
}; // Declare in fluid_playback.h initially; move to visual_backend.h in Task 3.
struct FluidSplat { float x, y, dx, dy; Color color; };
class FluidPlayback {
public:
    FluidPlayback(std::string_view preset, PlaybackSettings settings, uint32_t seed);
    bool select(std::string_view name);
    void next(); void prev(); void random();
    void setSettings(const PlaybackSettings& settings);
    void update(float dtSeconds);
    const FluidPreset& current() const;
};
class FluidMotion {
public:
    explicit FluidMotion(uint32_t seed);
    void addPcm(const float* stereo, size_t frames);
    std::vector<FluidSplat> update(float dtSeconds, float sensitivity,
                                 float ambientInterval, const FluidPreset& preset);
    std::vector<FluidSplat> seed(const FluidPreset& preset);
};
```

- [ ] Write failing tests for ordered next/prev wrap, deterministic shuffle, unknown selection preserving the active preset, selection resetting the timer, and lock stopping rotation while motion continues. A 100-second update must advance at most one preset, not replay missed changes.

```cpp
TEST_CASE("A locked fluid look keeps moving without rotating") {
    PlaybackSettings settings;
    settings.locked = true;
    FluidPlayback playback("lava", settings, 7);
    FluidMotion motion(7);
    playback.update(100.0f);
    CHECK(playback.current().slug == "lava");
    const auto splats = motion.update(100.0f, 0.0f, 2.5f, playback.current());
    CHECK(splats.size() == 1); // ambient only, no catch-up burst
}
```

- [ ] Write audio tests using literal stereo samples: zero input never triggers audio splashes; a strong onset does; another onset inside 120 ms does not; sensitivity zero suppresses audio but not ambient splashes; NaN/Inf samples are ignored; loss followed by silence does not reuse the previous energy indefinitely. Test many small versus one large elapsed interval for bounded output.
- [ ] Run tests red. Implement catalog-index selection with bounded elapsed time, deterministic randomness, and a complete preset reference rather than a mutable partial preset patch.
- [ ] Compute normalized RMS from the latest PCM block, treating nonfinite samples as zero and clipping magnitude to 1. Track whether fresh PCM arrived since the last motion update; no fresh data means zero input for that update. Use elapsed-time-aware envelope/baseline smoothing. Start with a threshold of 0.18 and baseline excess of 0.12 at sensitivity 1, scaled by sensitivity, matching the reference's intent without promising FFT equivalence. Apply the 120 ms cooldown and maximum three onset splashes.
- [ ] Generate spectrum/warm/mono/custom palette colors using the preset fields. Mono hue is in degrees. Colorful changes follow colorUpdateSpeed; non-colorful Ink retains its selected monochrome color. Carry over the reference's color-intensity scaling at the dye injection boundary rather than treating a normalized palette entry as an HDR dye quantity. Use unit-square positions and aspect-correct force scaling in the renderer. Seed a small bounded set of splashes immediately at initialization; thereafter emit at most one ambient splash when its interval expires.
- [ ] Run the complete core suite and sanitizers, then commit playback/motion behavior.

## Task 3: Transactional controller and MilkDrop adapter

**Files:** create `src/visual_backend.h`, `src/visual_controller.{h,cpp}`, `src/milkdrop_backend.{h,cpp}`, `tests/test_visual_controller.cpp`; modify `src/renderer.{h,cpp}`, `src/fluid_playback.h`, `CMakeLists.txt`.

**Consumes:** existing `Renderer`, `PresetCatalog`, `PresetManager`, `Command`, `Config`, and `PlaybackSettings` from Task 2.
**Produces:**

```cpp
struct VisualSelection { std::string playlist, preset; };
struct BackendInit {
    Config config;
    PlaybackSettings playback;
    VisualSelection selection;
    int width, height;
};
class VisualBackend {
public:
    virtual ~VisualBackend() = default;
    virtual void maintenance() = 0;
    virtual void addPcm(const float* stereo, size_t frames) = 0;
    virtual void render(float dtSeconds) = 0;
    virtual void resize(int width, int height) = 0;
    virtual void setSettings(const PlaybackSettings& settings) = 0;
    virtual void next() = 0;
    virtual void prev() = 0;
    virtual void random() = 0;
    virtual bool selectPreset(const std::string& name) = 0;
    virtual bool selectPlaylist(const std::string& name) = 0;
    virtual VisualSelection selection() const = 0;
};
using BackendFactory = std::function<std::unique_ptr<VisualBackend>(VisualMode, const BackendInit&)>;
class VisualController {
public:
    VisualController(Config config, int width, int height, BackendFactory factory);
    bool setMode(VisualMode mode);
    void apply(const Command& command); // output commands stay owned by main/OutputLevel
    void maintenance();
    void addPcm(const float* stereo, size_t frames);
    void render(float dtSeconds);
    void resize(int width, int height);
    VisualMode mode() const;
    std::string statusPreset() const;
};
```

- [ ] Move `PlaybackSettings` into `visual_backend.h` and update Task 2 imports without changing behavior. Keep this header free of GL/projectM/SDL includes.
- [ ] Write controller tests using a small backend test double at the external renderer boundary. Verify candidate construction receives saved selection and shared settings; thrown failure or a null factory result retains current mode, selection, and renderability; a successful switch destroys the previous backend only after candidate creation; selecting the already-active mode is a no-op. A null candidate is an initialization failure and must never replace the active backend.
- [ ] Exercise controller behavior rather than just fake counters: switch fluid→MilkDrop→fluid and inspect `mode()`/`statusPreset()`, change shared duration/lock, fail a switch and render another frame through the old backend. Track resource lifetime with a test-only RAII marker. Cover startup fluid failure falling back to MilkDrop and fallback failure propagating an exception.
- [ ] Implement mode selection transactionally:

```cpp
// Inside setMode, after checking target differs from the active mode:
auto candidate = factory_(target, initializationFor(target));
// Factory exceptions leave active_ untouched; log and return false.
savedSelectionFor(mode_) = active_->selection();
active_.swap(candidate);
mode_ = target;
// candidate now destroys the prior backend; initialize status from active_.
```

  `initializationFor()` and `savedSelectionFor()` are private controller helpers returning `BackendInit` and `VisualSelection&`; store one selection per enum value. Snapshot the outgoing selection before candidate construction but commit the snapshot only on success. Failed runtime render/maintenance/resize invokes one MilkDrop recovery attempt; a failing recovery throws to main's existing exit path. Never recursively retry recovery.
- [ ] Implement `MilkdropBackend` with declaration order Renderer→Catalog→PresetManager so teardown remains manager→catalog→renderer. `maintenance()` delegates to `PresetManager::tick()`. Preserve prior playlist/preset where available; otherwise use the existing startup fallback. Store the active playlist and filename in `selection()`.
- [ ] Separate brightness from `Renderer::render()`: provide `renderFrame()` for raw projectM drawing. Retain the current `render(brightness)` wrapper until Task 5 rewires main, avoiding a broken intermediate player build. No other projectM recovery logic changes.
- [ ] Run existing preset-manager tests, controller tests, core-only and player builds. Commit the adapter/controller increment; fluid factory support follows in Task 5.

## Task 4: Native fluid GPU renderer

**Files:** create `src/fluid_gl.{h,cpp}`, `src/fluid_shaders.{h,cpp}`, `src/fluid_renderer.{h,cpp}`, `tests/test_visual_gl.cpp`; modify `CMakeLists.txt`, `third_party/fluid/README.md`.

**Consumes:** `FluidPreset`, `FluidSplat`, `Config::Fluid`; upstream revision recorded in Task 1.
**Produces:**

```cpp
class FluidRenderer {
public:
    FluidRenderer(int width, int height, const Config::Fluid& config);
    ~FluidRenderer();
    void resize(int width, int height);
    void render(float dtSeconds, const FluidPreset& preset,
                const std::vector<FluidSplat>& splats);
};
```

- [ ] Add an opt-in `SIDESHOW_BUILD_GL_TESTS` CMake option requiring the player dependencies. Build `sideshow_gl_tests` as a separate real-context executable, excluded from default core CTest. Use SDL with a hidden drawable and the same GL version/profile as `Display`; failure to obtain a context must report an explicit unmet prerequisite, not a passed test.
- [ ] First write a real rendering test: initialize at 320×180, seed a deterministic colored splat, render several frames, read the default framebuffer, and assert nonzero finite color output. Observe failure before the solver is implemented. Add shader compilation and invalid-program cleanup cases.
- [ ] Implement fluid-local resource owners: program, texture/FBO, ping-pong target, VAO. Delete copy operations, support moves where needed, and delete handles on partial construction failure. Compile/link into temporary owners before publishing complete resources. Include useful shader/program diagnostics.
- [ ] Adapt the upstream shaders from the pinned `script.js`: base vertex, clear/copy, splat, curl, vorticity, divergence, pressure, gradient subtraction, advection with manual filtering fallback, bloom prefilter/blur/final, sunray mask/radial pass, and final display. Replace legacy varying/attribute/output/texture syntax with GL 3.3/GLES 3 forms, keeping algorithm coefficients. Use a fullscreen triangle and local shader strings, not runtime downloads.
- [ ] Probe candidate float target formats with `glCheckFramebufferStatus`, preferring R16F/RG16F and falling back to RGBA16F. Restore prior framebuffer/texture bindings after probes. If none are renderable, throw before activating fluid. When linear filtering cannot be used, use nearest filtering plus the upstream bilinear-advection shader variant. Do not infer renderability from extension names alone.
- [ ] Calculate aspect-correct field sizes from the configured shorter dimension. Check `GL_MAX_TEXTURE_SIZE`, positive drawable sizes, multiplication bounds, and all FBOs before replacing live targets. Ignore a zero-size/minimized resize until a positive size returns; a positive resize that cannot allocate fails transactionally.
- [ ] Implement the solver sequence exactly in this order, swapping ping-pong targets after writes:

```text
bounded splats → curl → vorticity → divergence → pressure decay
→ pressure iterations → gradient subtraction → velocity advection
→ dye advection → optional bloom → optional sunrays → final display
```

  Clamp simulation dt to [0, 1/60]. Use preset pressure and iteration count; use fixed quality allocations for the run. Implement palette-independent display shading, bloom parameters, sunray weight, and Overhead inversion. Generate any needed dithering noise locally with a fixed seed; do not import remote image assets.
- [ ] Establish explicit GL state at each pass (FBO, viewport, program, VAO, textures, blending/depth/scissor/color mask) so previous projectM rendering cannot corrupt fluid. Leave default framebuffer bound and a valid viewport before returning to shared output.
- [ ] Add real-context tests for every preset, manual-filter fallback, supported wider-format fallback, zero-size then positive resize, and repeated construction/destruction. Use framebuffer output and GL errors/resource counters at boundaries, not pixel-perfect cross-driver golden images. Run desktop GL checks; if GLES execution is unavailable, mark it unverified.
- [ ] Commit the renderer only after available real-context checks and core tests pass. Preserve explicit failures rather than silently disabling requested preset effects.

## Task 5: Fluid backend and application integration

**Files:** create `src/fluid_backend.{h,cpp}`; modify `src/{main.cpp,renderer.h,renderer.cpp,visual_controller.cpp}`, `tests/{test_visual_controller.cpp,test_visual_gl.cpp}`, `CMakeLists.txt`.

**Consumes:** backend/controller interfaces from Task 3 and playback/motion/renderer from Tasks 2/4.
**Produces:** `FluidBackend : VisualBackend` and a production backend factory used by main.

- [ ] Add failing tests for fluid status `fluid/lava`, mode-aware preset routing, rejecting non-default fluid playlists without changing the selection, and preserving shared settings on round trips. Confirm hardcut affects only MilkDrop while fluid keeps accepting audio/ambient splashes.
- [ ] Compose `FluidBackend` with `FluidPlayback`, `FluidMotion`, `FluidRenderer`, and `PlaybackSettings`. `maintenance()` is a no-op; `render(dt)` updates playback and motion, then passes preset/splats to the renderer. `addPcm()` forwards to motion; `resize()` forwards to the renderer. Seed visible dye once after construction. Selecting a preset restarts rotation; it does not reset the ongoing field.
- [ ] Implement the production factory with exactly two branches, constructing `MilkdropBackend` or `FluidBackend`. No registration/discovery system. Initialize from saved mode selection and current shared playback settings.
- [ ] Rewire main to own Display→VisualController→BrightnessPass and existing capture/OSC/output objects in safe lifetime order. Preserve this frame sequence:

```text
SDL events → controller maintenance → bounded OSC command processing
→ capture retry / AudioFeed → output smoothing
→ active backend render(dt) → shared BrightnessPass → swap/status/frame pacing
```

  Only `master` and `blackout` mutate `OutputLevel`; other commands route through controller. Replace raw `PresetManager` status with `controller.statusPreset()`. Keep the OSC reply types unchanged. Pass the existing stereo PCM chunks through the controller; keep projectM's 480-frame feed limit and full silence flush intact.
- [ ] Move ownership of the brightness pass out of the projectM renderer and remove the transitional wrapper from Task 3. Apply brightness exactly once after either backend, even for an inverted/bright-background preset. Failure recovery must complete before drawing the final output pass.
- [ ] Add real-context tests: render both modes under blackout and assert black pixels; restore master and require visible output; repeat mode switches and resizing; simulate failed candidate creation and verify the old mode still draws. Track lifetime counts in test fixtures and check GL errors after switching.
- [ ] Run all C++/Python suites, player build, and real-context integration tests. Commit the integrated feature.

## Task 6: Operator documentation, end-to-end validation, and review

**Files:** modify `README.md`, `docs/architecture.md`, `docs/configuration.md`, `docs/development.md`, and the new test files only for demonstrated coverage gaps.

- [ ] Document OSC `mode`, all eleven slugs, fluid playlist semantics, hardcut's MilkDrop-only behavior, startup TOML settings, quality ranges, and transient mode/preset memory. Show commands for one panel and all panels. Explain cut transitions and that animation restarts when returning to a mode.
- [ ] Extend the hardware checklist with quiet-room ambient motion, onset response, mic unplug/replug, each preset's effects, blackout during mode switches, unavailable float-target fallback, and sustained operation. Record measured FPS/hardware if available; explicitly distinguish desktop validation from Pi validation.
- [ ] Validate a clean core-only configuration and run sanitizers using the existing locally fetched dependencies when offline. Build/run the full player and optional GL tests. Exercise the real OSC CLI against a local player: mode fluid, preset lava, lock, master, blackout, and return to MilkDrop; verify ping output and logs.

```bash
cmake -S . -B build -DSIDESHOW_BUILD_PLAYER=ON -DSIDESHOW_BUILD_GL_TESTS=ON \
  -DCMAKE_PREFIX_PATH="$PWD/deps/install"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python3 -m unittest discover -s tools
./build/sideshow_gl_tests
git diff --check
```

- [ ] Request independent review of the completed branch. Pay particular attention to partially constructed GL resources, backend replacement, callback lifetime, shared state, audio staleness, and config/protocol compatibility. Fix concrete findings and rerun affected checks followed by the full suite.
- [ ] Report implemented behavior, passed checks, and any unmet real-context/Pi prerequisites. Do not claim unavailable GPU checks passed. Commit verified code/documentation; merge/push only when requested for this feature.

## Plan self-review

- All spec sections map to Tasks 1–6, including Overhead inversion, bloom/sunrays, attribution, per-mode memory, and startup/runtime fallback.
- Shared types are defined once: mode/presets in Task 1; playback/motion in Task 2; backend/controller records in Task 3. Later tasks use these interfaces.
- The five review-focus conditions each have an owning task and explicit behavior checks.
- Test doubles are restricted to the GPU/backend boundary for controller tests; actual shader compilation and framebuffer results require real GL tests.
- There is no feature implementation in this document-only planning step. Execution begins after the user reviews the plan and selects an execution approach.
