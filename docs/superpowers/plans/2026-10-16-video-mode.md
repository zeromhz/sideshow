# Selectable Silent Video Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Play a recursive local video library silently through OSC as a separate visualization mode, with loop/hold/next end behavior and responsive stage controls.

**Architecture:** Pure catalog/playback logic feeds one application-owned FFmpeg worker. A video backend presents completed RGBA frames on the main GL thread; the controller retains the old visualizer until a candidate's first frame is uploaded successfully. Generation IDs, bounded queues and explicit preparation state keep cancellation, mode changes and clip replacement deterministic.

**Tech Stack:** C++17, CMake 3.20+, FFmpeg 5.1+ (libavformat/libavcodec/libavutil/libswscale), existing SDL2, GL 3.3/GLES 3, liblo, toml++, Catch2 and standard-library Python.

**Spec:** `docs/superpowers/specs/2026-10-16-video-mode-design.md` (approved).

## Global Constraints

- Playback is always silent; no audio decoder or output device is created for video.
- MilkDrop remains the startup default. Video and future ISF are separate modes, not layers.
- Default root: `/opt/sideshow/videos`; development root: `deps/videos`.
- Stable clip IDs are exact, case-sensitive relative paths with `/` separators, including the extension.
- Candidate extensions, case-insensitive: `.mp4`, `.m4v`, `.mov`, `.mkv`, `.webm`, `.avi`.
- Folder collections contain only their direct video children; `playlist .` selects root children.
- Ignore hidden entries and symlinks; reject absolute paths, `..`, embedded NULs, URL inputs and paths outside the canonical root.
- `video_end` accepts `loop`, `hold`, `next`; loop is the default. `video_pause` accepts int 0/1; `video_restart` takes no argument.
- Video-specific controls outside active or pending video do nothing with a warning.
- Lock suppresses automatic clip changes; end-next behaves as loop while locked. Explicit navigation still works.
- Returning to video restarts the remembered clip unpaused, retaining end behavior. Explicit clip/restart retains pause.
- Preserve master/blackout, bounded command processing, microphone capture and pong's `string,string,float` signature.
- Single application-owned decoder worker; at most one current plus one candidate decoder context.
- At most three completed converted frames in total, plus one being converted and the current GPU image.
- No decoding, scanning, blocking worker wait or thread join in the rendering loop.
- Five-second deadline for open/probe/seek/packet-read operations, with cooperative cancellation; no hard real-time guarantee for codec/filesystem stalls.
- Source dimensions: each dimension at most 4096 and at most 8,847,360 source pixels.
- Converted output fits 1920×1080 landscape or 1080×1920 portrait, respecting GL texture limits and display aspect ratio.
- Progressive SDR only; reject HDR and interlacing, preserve matrix/range metadata, sample aspect and common 90-degree rotation.
- FFmpeg 5.1 or newer; core-only builds stay free of FFmpeg, SDL, GL and projectM requirements.
- No streaming, arbitrary seek/speed/reverse, audible video, mixing, live rescan, HDR tone mapping, hardware acceleration or ISF implementation in this plan.
- Pi/GLES performance, physical microphone recovery and sustained thermals remain unverified without target hardware.

## Review Focus

1. `mode video` followed immediately by `preset`, pause, or another mode must not route to the wrong backend or activate a stale candidate (Tasks 4–5).
2. Cancellation/shutdown while decoding or waiting on a full queue must wake the worker, release resources and avoid a render-thread join (Tasks 3–5).
3. Reordered/variable timestamps, EOF and pause/restart must not lose trailing frames, spin on EOF, or cause catch-up bursts (Tasks 2–3).
4. Nested duplicate names, symlinks, deleted/replaced files and unreadable directories must not select the wrong clip or escape the library (Tasks 1, 3).
5. A failed first-frame upload, contaminated sampler/pixel-store state, or switching under blackout must preserve the old picture and clean up candidate resources (Tasks 4–5).

## Execution setup and baseline

Use the user's preserved preference for native execution and isolated worktrees unless they request a change. Apply the worktree skill's existing-isolation checks; start from the approved spec commit, never from a stale remote default. No product code or new dependencies are installed during planning. Do not merge or push without a request for this feature.

Read the spec, inspect repository instructions, then run:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python3 -m unittest discover -s tools
```

Expected: current player builds, 73 C++ tests and 10 Python tests pass. In a fresh worktree configure with the existing local Catch2/toml++ sources and projectM installation rather than repeating machine provisioning. FFmpeg libraries are already discoverable on this Mac; check their versions and compile against the supported API range.

Before product edits, create the executing skill's per-plan ledger and record interface conflicts/rulings. All new C++ types below live in `namespace sideshow`; public headers for pure logic must not include FFmpeg/GL/SDL headers. Pseudocode below specifies control order; use existing repository error/logging conventions.

## File ownership map

| Files | Responsibility |
|---|---|
| `src/video_types.{h,cpp}` | End modes, video commands, frame/error records, path syntax validation |
| `src/video_catalog.{h,cpp}` | Cancellable recursive discovery, exact IDs, direct-child collections, safe lookup |
| `src/video_playback.{h,cpp}` | Presentation clock, pause/end/lock transitions and timestamp normalization |
| `src/video_decoder.{h,cpp}` | One FFmpeg context, demux/decode/drain/rewind/conversion; worker-thread only |
| `src/video_worker.{h,cpp}` | Single thread, generation IDs, request/result bounds and cancellation |
| `src/video_control.h` | Narrow optional control seam used only by VideoBackend/controller |
| `src/video_presenter.{h,cpp}` | Texture upload, aspect/rotation geometry, GL state and cleanup |
| `src/video_backend.{h,cpp}` | Catalog/session/playback/presenter composition, preparation and selection |
| `src/visual_backend.h`, `src/visual_controller.{h,cpp}` | Pending preparation and safe mode activation |
| Existing config, protocol, CLI, CMake and setup files | Mode/config/dependency integration |
| `tests/test_video_*.cpp`, `tests/support/video_*.h` | Pure, decoder/worker and real GL behavior tests |
| `tests/fixtures/video/`, `tools/generate_video_fixtures.py` | Small generated media and reproducible provenance |
| README and existing docs | Operators, development and hardware acceptance |

Keep existing Fluid-local GL helpers local. Video needs its own small state/texture owners, or extract only a demonstrated shared owner without broad graphics refactoring. Do not modify projectM's preset manager.

## Task 1: Recursive library, configuration and OSC contract

**Files:** create `src/video_types.{h,cpp}`, `src/video_catalog.{h,cpp}`, `tests/test_video_catalog.cpp`; modify `src/{visual_mode.h,visual_mode.cpp,command.h,osc_parse.cpp,config.h,config.cpp}`, `tests/{test_config.cpp,test_osc_parse.cpp}`, `tools/{osc_send.py,test_osc_send.py}`, `setup/{config.example.toml,config.dev.toml}`, `CMakeLists.txt`.

**Interfaces:**

```cpp
enum class VideoEnd { Loop, Hold, Next };
std::optional<VideoEnd> parseVideoEnd(std::string_view);
std::string_view videoEndName(VideoEnd);
bool validVideoId(std::string_view); // relative file ID; rejects '.' as a clip
struct VideoClip { std::string id, folder; std::filesystem::path path; };
class VideoCatalog {
public:
    static VideoCatalog scan(const std::filesystem::path& root,
                             const std::function<bool()>& cancelled);
    const std::vector<VideoClip>& clips() const;
    std::vector<std::string> inFolder(std::string_view folder) const;
    std::optional<VideoClip> find(std::string_view id) const;
    std::optional<std::filesystem::path> resolveForOpen(std::string_view id) const;
    const std::vector<std::string>& warnings() const;
};
// Add VisualMode::Video and a named mode-count constant for indexed state.
// Add cmd::VideoEndMode { VideoEnd value; }, cmd::VideoPause { bool on; },
// cmd::VideoRestart {} to Command.
// Config::Video { std::string root="/opt/sideshow/videos", preset="";
//                 VideoEnd end=VideoEnd::Loop; } video;
```

- [ ] Write catalog tests with a real temporary tree, not a filesystem mock. A fixture helper `VideoTree` in `tests/support/video_tree.h` creates/removes only its own unique temporary directory and exposes `root`, `file(relative)`, and `symlink(relative,target)`.

```cpp
TEST_CASE("Video IDs distinguish nested duplicate basenames") {
    VideoTree tree;
    tree.file("intro.mp4"); tree.file("a/intro.mp4"); tree.file("a/b/deep.MOV");
    auto catalog = VideoCatalog::scan(tree.root, [] { return false; });
    CHECK(catalog.inFolder(".") == std::vector<std::string>{"intro.mp4"});
    CHECK(catalog.inFolder("a") == std::vector<std::string>{"a/intro.mp4"});
    REQUIRE(catalog.find("a/b/deep.MOV"));
    CHECK_FALSE(catalog.find("deep.MOV"));
    CHECK_FALSE(catalog.resolveForOpen("../outside.mp4"));
}
```

- [ ] Add table cases for hidden files/directories, case-sensitive IDs with case-insensitive extensions, non-video entries, root and directory symlinks, a symlink substituted after scan, deleted files, empty/missing root, cancelled scanning, inaccessible entries and stable sort order. For permission cases skip only when the test identity can actually read the forbidden directory; report that skip.
- [ ] Add tests for `mode video`, `video_end loop|hold|next`, exact int 0/1 pause, argument-free restart, extra arguments and malformed types. Test `video.preset=""` defaults, invalid end/root types, and valid nested IDs. Python tests must encode/decode actual command packets, including paths with spaces and rejection of invalid pause/end arguments.
- [ ] Run the new tests and observe missing behavior. Register sources/tests without FFmpeg linkage in `sideshow_core`. Expected: new cases fail, existing cases remain unchanged.
- [ ] Implement catalog traversal with error-code filesystem APIs and cancellation between entries; never follow symlinks. Resolve IDs by components and canonical containment, not string-prefix matching. Read-only traversal errors become warnings and must not discard already discovered siblings. `resolveForOpen` rechecks all components and file type. Reject protocol syntax and embedded NUL before converting to a filesystem path.

```text
scan entry:
  if cancelled: stop with cancelled result/warning
  if hidden or symlink: skip (disable recursion for that entry)
  if regular file with eligible extension:
    record normalized relative ID and parent folder ('.' at root)
sort records by ID; construct exact lookup and direct-child folder index
```

- [ ] Add config/OSC/CLI support. Reject invalid video pause values rather than inheriting the existing generic nonzero-toggle coercion. Preserve old commands' behavior. Until Task 4, explicitly reject video backend activation with a warning and retain the current backend; do not let an enlarged enum index the existing two-element array.
- [ ] Run full core/Python suites and player build. Expected: all pass. Commit the catalog/protocol increment.

## Task 2: Pure playback clock, timestamps and end behavior

**Files:** create `src/video_playback.{h,cpp}`, `tests/test_video_playback.cpp`; extend `src/video_types.h`; modify CMake.

**Consumes:** `VideoEnd`, decoded timestamp/duration values, explicit elapsed time and ordered clip IDs.
**Produces:**

```cpp
struct VideoFrame {
    uint64_t generation=0;
    std::string clipId;
    double pts=0, duration=1.0/30.0;
    int width=0, height=0, stride=0;
    double pixelAspect=1;
    int rotationDegrees=0;
    std::vector<uint8_t> rgba;
};
enum class VideoAction { None, Rewind, Advance };
class VideoPlayback {
public:
    explicit VideoPlayback(VideoEnd end=VideoEnd::Loop);
    void beginClip(); // resets timeline/EOF but retains end, lock and pause
    void advance(double elapsedSeconds);
    void setPaused(bool); bool paused() const;
    void setLocked(bool);
    void setEnd(VideoEnd); VideoEnd end() const;
    void markEof(double endSeconds);
    VideoAction takeAction(); // at most once for a given EOF/control transition
    double position() const;
};
class VideoTimestamps {
public:
    explicit VideoTimestamps(double fallbackFps);
    std::pair<double,double> normalize(std::optional<double> pts,
                                      std::optional<double> duration);
    void reset();
};
```

- [ ] Write failing tests for playback actions and clock semantics before implementation. These literal times pin user-visible transitions rather than private fields.

```cpp
TEST_CASE("Pause and lock preserve looping without advancing clips") {
    VideoPlayback clock(VideoEnd::Next);
    clock.setLocked(true); clock.beginClip(); clock.markEof(1.0);
    clock.setPaused(true); clock.advance(10); CHECK(clock.position() == 0);
    CHECK(clock.takeAction() == VideoAction::None);
    clock.setPaused(false); clock.advance(1.0);
    CHECK(clock.takeAction() == VideoAction::Rewind);
    CHECK(clock.takeAction() == VideoAction::None);
}
TEST_CASE("Changing held EOF to next acts once") {
    VideoPlayback clock(VideoEnd::Hold);
    clock.markEof(1.0); clock.advance(2);
    CHECK(clock.takeAction() == VideoAction::None);
    clock.setEnd(VideoEnd::Next);
    CHECK(clock.takeAction() == VideoAction::Advance);
    CHECK(clock.takeAction() == VideoAction::None);
}
```

- [ ] Add cases for each end/lock/pause combination, unlock before the next EOF, restart while paused, new clip preserving pause/end, negative/NaN elapsed time, a long stall, EOF notification arriving before the final frame is due, and repeated mode changes while held. Navigation target selection stays in the backend/catalog; clock emits actions only.
- [ ] Timestamp tests: first PTS 12.5 becomes 0; next PTS 12.54 becomes .04; absent/invalid timestamps advance at stream rate; nonmonotonic output never goes backward; invalid rate uses 30 FPS; reset begins a new epoch. Reordered codec output is normalized after the decoder returns presentation-order frames, not before packet decoding.
- [ ] Run red, then implement finite/nonnegative time checks and explicit EOF-action latching. Pause advances no time. `beginClip()` clears held/EOF/action state without clearing pause/end/lock. New end policy at held EOF resets the latch only if behavior actually changes.

```text
takeAction:
  if paused or EOF unknown or position < end: None
  effective = (locked and end==Next) ? Loop : end
  if effective==Hold or this EOF action was issued: None
  latch action; return effective==Loop ? Rewind : Advance
```

- [ ] Run all core tests and core ASan/UBSan. Expected: green, no unbounded catch-up loops or stale actions. Commit playback logic.

## Task 3: Real FFmpeg decoder and bounded worker

**Files:** create `src/video_decoder.{h,cpp}`, `src/video_worker.{h,cpp}`, `tests/test_video_decoder.cpp`, `tests/test_video_worker.cpp`, `tests/support/video_fixtures.h`, `tools/generate_video_fixtures.py`, `tests/fixtures/video/README.md` and generated fixtures; modify CMake and setup scripts. `video_fixtures.h` defines `std::filesystem::path videoFixture(std::string_view name)` resolving against a CMake-provided absolute fixture directory, independent of the process working directory. Decoder tests include Catch2 test macros and `catch_approx.hpp`.

**Consumes:** `VideoCatalog`, `VideoFrame`, `VideoTimestamps`, configured root and immutable open requests.
**Produces:**

```cpp
enum class VideoEventKind { Catalog, Frame, Eof, Failed, Released };
struct VideoEvent {
    VideoEventKind kind;
    uint64_t session=0, generation=0;
    std::optional<VideoFrame> frame;
    std::shared_ptr<const VideoCatalog> catalog;
    std::string error;
    double endSeconds=0;
};
struct VideoOpenRequest {
    uint64_t session=0, generation=0;
    std::string clipId;
};
class VideoSource { // blocking media boundary; used only by the worker
public:
    virtual ~VideoSource()=default;
    virtual std::optional<VideoFrame> next()=0;
    virtual void rewind()=0;
};
class VideoDecoder final : public VideoSource { // PIMPL, FFmpeg-free header
public:
    VideoDecoder(const std::filesystem::path& file, std::function<bool()> cancelled);
    ~VideoDecoder();
    std::optional<VideoFrame> next() override; // null only for drained EOF; throws on failure
    void rewind() override;
};
using VideoDecoderFactory = std::function<std::unique_ptr<VideoSource>(
    const std::filesystem::path&, std::function<bool()>)>;
class VideoWorker { // application-owned, main-thread methods are nonblocking
public:
    explicit VideoWorker(std::filesystem::path root, VideoDecoderFactory factory={});
    ~VideoWorker(); // cancel, notify, join; invoked at application shutdown only
    uint64_t acquireSession(); // starts cancellable scan if catalog not cached
    void open(VideoOpenRequest);
    void commit(uint64_t session, uint64_t generation);
    void cancel(uint64_t session); // invalidates results and releases contexts off-thread
    void rewind(uint64_t session, uint64_t newGeneration);
    void playback(uint64_t session, double position, bool paused);
    std::optional<VideoEvent> poll(uint64_t session);
};
```

`rewind` uses a newly allocated generation, invalidates old queued frames/EOF, and emits a fresh first-frame event before the backend resets its timeline. Generations increase across every open and rewind; do not recycle them at loop boundaries.

`open` prepares a candidate while retaining the current decoder; its first-frame event is tagged with the requested generation. `commit` promotes that candidate after successful GL upload and then releases the previous decoder. A failed/superseded candidate is destroyed on the worker; the current decoder may resume. `cancel` applies even with a full queue; stale-session `open`/`commit` cannot resurrect it. Use one latest pending open slot, coalesced control state and bounded event storage, not an unbounded task queue. Catalog, error, EOF and release messages have reserved/coalesced slots separate from the three-frame capacity. There is only one video session in active/pending use; wait asynchronously for release acknowledgment before preparing a replacement session.

- [ ] Add `SIDESHOW_BUILD_VIDEO_TESTS` (default OFF) to enable decoder/worker tests without a GPU. Enable FFmpeg dependency discovery when either that option or player is ON. Require pkg-config floors `libavformat>=59.27`, `libavcodec>=59.37`, `libavutil>=57.28`, `libswscale>=6.7`; compile against FFmpeg 5.1 and modern headers without deprecated-field assumptions. Keep core-only configuration independent.
- [ ] Generate and commit tiny deterministic fixtures. Use FFmpeg CLI only for fixture generation, not runtime playback; record commands and tool version. Provide progressive SDR color bars with audio, three-frame B-frame H.264, variable-duration frames, rotated/sample-aspect content, audio-only, truncated/corrupt, interlaced and HDR-tagged fixtures. Prefer native MPEG-4/FFV1/PCM encoders where sufficient; if fixture generation needs libx264, check availability and report it rather than downloading media. Synthetic timestamp unit tests cover absent timestamps where muxers repair them.

```bash
ffmpeg -f lavfi -i testsrc2=size=64x48:rate=10 -f lavfi -i sine=frequency=440 \
  -t 0.5 -c:v mpeg4 -bf 2 -c:a pcm_s16le tests/fixtures/video/bars-audio.avi
ffmpeg -f lavfi -i testsrc2=size=64x48:rate=5 -t 0.6 \
  -c:v libx264 -bf 2 -pix_fmt yuv420p tests/fixtures/video/reordered.mp4
```

- [ ] Write real decoder tests: read all frames, assert dimensions and increasing normalized times, rewind and compare initial image, reach drained EOF exactly once, reject audio-only/corrupt/HDR/interlaced sources. Add tests of pure dimension/stride/output-size helpers for overflow and excessive limits without allocating giant fixtures. No mock FFmpeg calls for ordinary playback.

```cpp
TEST_CASE("Decoder drains reordered trailing frames before EOF") {
    VideoDecoder decoder(videoFixture("reordered.mp4"), [] { return false; });
    std::vector<double> times;
    while (auto frame=decoder.next()) times.push_back(frame->pts);
    REQUIRE(times.size()==3);
    CHECK(times[0]==Catch::Approx(0));
    CHECK(times[1]==Catch::Approx(.2));
    CHECK(times[2]==Catch::Approx(.4));
}
```

- [ ] Implement RAII ownership for format, codec, packet, frame, scaler and input handles; no GL calls from decoder/worker. Open a validated regular file through an owned local-file AVIO adapter (no URL fallback). Reject external data references/nested-resource opens with the demuxer's I/O-open callback. This enforces local-library input even for containers that can reference other files. Check cancellation, five-second operation deadline and probing caps; cap decoder threads at two, probe bytes at 5 MiB and analysis duration at five seconds.
- [ ] Implement send/receive correctly: preserve a packet when send returns EAGAIN, receive until EAGAIN, send a null packet once at demux EOF, and drain until decoder EOF. Select a non-attached-picture video stream; never initialize audio decoding. Convert progressive SDR output with libswscale and explicit color range/matrix, honoring output bounds, pixel aspect, rotation and dynamic format changes. Reject dimensions before opening the codec where metadata permits and again on decoded frames.

```text
next:
  receive frame -> validate, normalize timestamp, convert, return
  receive EAGAIN -> read video packet, send without dropping on EAGAIN
  demux EOF -> send null once, continue receiving delayed frames
  decoder EOF -> return null
  cancellation/deadline/other error -> unwind owned resources, report error
```

- [ ] Write worker tests before its implementation. Use real tiny fixtures for ordinary behavior, and a test-only injected decoder factory at the blocking-I/O boundary for deterministic cancellation/deadline cases. Use the `VideoDecoderFactory` constructor seam above; an empty factory creates `VideoDecoder`. Test substitutes implement `VideoSource`, preserve first-frame/EOF semantics, and honor the supplied cancellation callback. Assert real worker behavior: completed-frame storage never exceeds three; pending open collapses to the latest generation; cancel wakes full-queue waits; old session cannot reactivate; shutdown completes after cooperative decoder cancellation. Compare worker results/frames, not the mock's existence.
- [ ] Implement the single worker, session/generation arbitration, cached catalog, candidate handshake and clock/paused state. Check control updates between bounded decode batches (at most 64 packet operations before revisiting control state). Skip obsolete decoded frames before expensive conversion when safely possible; never allow skipped frames to lose EOF or first-frame readiness. While paused, decode enough to prepare a requested first frame and then respect backpressure. Release sessions on the worker and send acknowledgment; do not block backend destruction.
- [ ] Update macOS provisioning to install `ffmpeg`; Pi provisioning installs `libavformat-dev libavcodec-dev libavutil-dev libswscale-dev`. Create the videos directory without changing existing contents. Document FFmpeg build/license/codec variability; no vendored codec binaries.
- [ ] Run decoder/worker tests, core/Python suites, a player build and decoder ASan/UBSan. Expected: all pass; FFmpeg 5.1 compatibility is checked with actual headers/build when available and otherwise explicitly listed as unverified. Commit decoder and worker together as one tested subsystem.

## Task 4: Asynchronous controller preparation and video control routing

**Files:** create `src/video_control.h`; modify `src/visual_backend.h`, `src/visual_controller.{h,cpp}`, `tests/test_visual_controller.cpp`; add `tests/support/pending_video_backend.h` and update CMake if needed.

**Consumes:** existing backend factory, `VisualMode::Video`, video command records, `VideoEnd`.
**Produces:**

```cpp
enum class PreparationState { Pending, Ready, Failed };
struct PreparationResult { PreparationState state; std::string error; };
class VideoControl {
public:
    virtual ~VideoControl()=default;
    virtual void setEnd(VideoEnd)=0;
    virtual VideoEnd end() const=0;
    virtual void setPaused(bool)=0;
    virtual void restart()=0;
};
// Extend VisualBackend with non-pure defaults:
// virtual PreparationResult preparation() { return {PreparationState::Ready,{}}; }
// virtual VideoControl* videoControl() { return nullptr; }
// Add BackendInit::videoEnd, default VideoEnd::Loop.
// Existing backends remain ready and have no video control seam.
```

The controller owns `active_`, optional `pending_`, and `pendingMode_`; preparation is polled during maintenance. Replace the fixed two-slot selection array with enum-count-sized storage and explicit initialized entries. Keep `setMode` returning whether a request was accepted (pending counts as accepted); `mode()` and status describe only the active backend.

- [ ] Add failing tests using a deterministic pending backend at the GPU/decoder boundary. Its readiness is externally advanced by the test, its selection uses real catalog-style IDs, and its video control fields model the seam. Verify: old backend still renders; active status unchanged until ready; mode-video then preset goes to pending; a second mode cancels pending; returning to already-active mode cancels pending; an old ready notification cannot activate later.
- [ ] Cover shared settings applied to active and pending, video controls ignored outside video, remembering end behavior across visits, returning unpaused, null/throw/failed candidates, resize during preparation, failed preparation keeping active selection, and fatal Video GL recovery attempting MilkDrop once. Retain all original Fluid/MilkDrop tests.

```text
setMode(target):
  cancel a different pending request
  if target is active: succeed without restarting
  construct candidate; apply current dimensions/settings/end state
  if failed: log; retain active
  if pending: store candidate, keep active
  if ready: snapshot outgoing selection/end; swap; release outgoing backend
maintenance:
  maintain active
  poll pending (which may process first-frame upload)
  ready -> commit; failed -> discard and log; pending -> retain both
selection/control command:
  route to pending if it exists, otherwise active
shared settings:
  update active and pending; output commands remain owned by main
```

- [ ] Resolve startup without blocking: when startup mode is Video, initialize a MilkDrop fallback first, then request Video asynchronously. An unavailable video library leaves fallback running. If fallback itself cannot initialize, propagate the startup error through main's existing exit path. Preserve startup behavior for the two existing modes.
- [ ] Implement cancellation and preparation with no worker join in destructors on the frame path. During preparation, resize both backends; catch candidate-only failure without recovering or discarding the healthy active mode. Extend fatal active recovery to Video as well as Fluid; never retry a failed recovery recursively.
- [ ] Run all core/Python tests and player build. Expected: green before a real VideoBackend exists; production attempts to activate Video still fail explicitly at the factory until Task 5. Commit controller integration.

## Task 5: Video presenter, backend and real mode integration

**Files:** create `src/video_presenter.{h,cpp}`, `src/video_backend.{h,cpp}`, `tests/test_video_gl.cpp`; modify `src/main.cpp`, `CMakeLists.txt`, README and docs.

**Consumes:** Tasks 1–4 contracts, shared `VideoWorker&`, existing `BrightnessPass`.
**Produces:**

```cpp
class VideoPresenter {
public:
    VideoPresenter(int drawableWidth,int drawableHeight);
    ~VideoPresenter();
    void upload(const VideoFrame&); // commit texture only after validation/upload succeeds
    void resize(int width,int height);
    void render(); // aspect fit, black background, opaque video
};
class VideoBackend final : public VisualBackend, public VideoControl {
public:
    VideoBackend(const BackendInit&, VideoWorker&);
    ~VideoBackend(); // cancel session; never join
    PreparationResult preparation() override;
    VideoControl* videoControl() override { return this; }
    // Implement every existing VisualBackend method plus VideoControl methods.
};
```

- [ ] Write a separate real-context `sideshow_video_gl_tests` executable, opt-in with the existing GL-test option. Reuse SDL context setup helpers only where useful. Missing GL context is an unmet prerequisite/failure, never a passed GPU test. Test presenter upload with synthetic RGBA quadrant frames before decoding integration.

```text
320x180 drawable + square red video:
  read pixel (0,90) -> black bar
  read pixel (160,90) -> red image
rotate a labeled quadrant image 90 degrees -> assert expected corner colors
apply shared blackout -> all RGB pixels zero
```

- [ ] Add failure/state tests: invalid stride/truncated frame payload, oversized upload, zero then positive resize, nondefault unpack row length/alignment/PBO binding, active texture unit outside Video's units, mipmap sampler, scissor/depth/color-mask contamination. Failed candidate upload must preserve old texture and return allocated handles to baseline using test-only GL tracking as in Fluid tests.
- [ ] Implement main-thread-only RAII program/VAO/texture upload. Clear to black and aspect-fit using display aspect, including sample aspect and rotation. Set and restore upload state explicitly; bind sampler zero on the video unit. Guard candidate initialization and failed uploads so old renderer state remains usable. Use shaders compatible with GL 3.3/GLES 3 and apply brightness only in main.
- [ ] Implement backend session acquisition and preparation: poll catalog, choose configured/remembered clip or bounded automatic fallback, request first frame, validate generation, upload it, then acknowledge worker commit and expose Ready. Never claim activation merely because a decoder opened. Keep active clip selection unchanged until candidate upload succeeds. Main-thread polling drains only bounded event slots and presents the latest due frame; it never waits.
- [ ] Implement direct-child navigation, explicit selection/restart, sorted folder selection, random without immediate repeat unless singleton, and bounded EOF-next retries. Distinguish startup/automatic fallback from explicit load failure. Route clock end actions to rewind/advance once; pause/resume uses the same timeline in worker and presenter. EOF waits until the trailing frame's end time, and a paused load still exposes its first frame. Decoder failures hold the last picture and stop retrying.
- [ ] Integrate main ownership in this order: Display, VideoWorker, VisualController, BrightnessPass and remaining I/O objects; reverse destruction therefore cancels backend sessions before joining the worker and destroys GL objects before Display. Capture the worker by reference in the production factory; do not create a worker per mode change. Other modes keep receiving existing AudioFeed behavior; Video's `addPcm` is a no-op.
- [ ] Add real-decoder/GL integration tests using the committed fixtures: startup Video, immediate mode+clip+pause, looping/held/next EOF, paused restart, nested paths with spaces, failed clip preserving picture, stale request ignored, rapid mode switches, blackout throughout switching, and shutdown during pending load. Assert visible image data and status, not only callback counts.
- [ ] Update operator docs/config examples with every command, folder semantics, silent playback, retained settings, startup fallback, supported media limits and copying files to the device. Document the short warning-only behavior of controls outside video and that new files require restart. Preserve the ISF follow-on request explicitly in the design history; do not add placeholder ISF enum values or unused shader infrastructure.
- [ ] Run the complete validation set and inspect actual output:

```bash
cmake -S . -B build -DSIDESHOW_BUILD_PLAYER=ON -DSIDESHOW_BUILD_GL_TESTS=ON \
  -DSIDESHOW_BUILD_VIDEO_TESTS=ON -DCMAKE_PREFIX_PATH="$PWD/deps/install"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python3 -m unittest discover -s tools
./build/sideshow_gl_tests
./build/sideshow_video_gl_tests
```

Expected: all unit/decoder/worker suites and both real-context executables pass. Repeat core and decoder sanitizer configurations after final code changes. Verify clean core-only configure/build without FFmpeg discovery. Launch a local player and use the real Python CLI for nested clip selection, pause/restart, all end policies, lock, blackout and return to Fluid/MilkDrop; ping must show the active `video/<relative-id>`.
- [ ] Record desktop resolution/codec/FPS measurements without calling them Pi results. Expand the Pi checklist for H.264 720p/1080p, late decoder frames, long loops, corrupt/deleted files, mode switches under blackout and thermal behavior. Report unavailable FFmpeg 5.1 or Pi validation explicitly.
- [ ] Commit the integrated feature and documentation after available checks pass. Request one independent whole-branch review for native execution, particularly preparation/cancellation races, FFmpeg ownership, queue bounds, path containment, EOF timing and GL state. Fix important findings with reproducing tests, rerun affected checks and the full suites, and commit verified fixes. Merge/push only when requested for this feature.

## Self-review and handoff

- Catalog, media support/limits, protocol, timing, pending control routing, failure recovery, resource ownership and validation sections each map to Tasks 1–5.
- All five Review Focus conditions have explicit tests in their owning tasks.
- The worker owns two decoder contexts at most, not two workers; three completed converted frames is a global worker bound, not a bound per decoder.
- Every rewind gets a new generation; the decoder factory returns the declared `VideoSource` boundary, and fixture lookup is defined independently of working directory.
- Preparation commits only after main-thread upload; cancellation and cleanup cannot join in the render path. Existing synchronous backends stay compatible through default preparation methods.
- Video settings are separate from shared visualizer duration/shuffle; clock tests cover hold, pause and lock interactions.
- Startup fallback, pending-mode status, folder selection, explicit-load failure and mode-return behavior follow the approved specification.
- ISF stays a subsequent independently designed deliverable. No ISF product code belongs in this plan.
- Recommended execution: native, preserving the previous session preference, because these five tasks share closely related lifecycle interfaces. One independent branch review remains required.
