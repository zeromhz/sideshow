# Video implementation validation — 2026-10-16

Branch: `codex/selectable-video-mode`, based on `ba17b1a`.

Implemented the approved video spec and all five plan stages. Video is silent and
OSC selectable; ISF remains a separately designed follow-on feature.

Verified on macOS:

- Full player build, 96 C++ tests and 11 Python tests.
- Real Fluid/projectM and Video SDL/OpenGL integration executables.
- Core and FFmpeg/worker AddressSanitizer + UndefinedBehaviorSanitizer suites.
- Fresh core-only configure/build/test without FFmpeg/SDL/GL dependency discovery.
- Real localhost OSC for nested paths with spaces, mode switching, pause/restart,
  loop/hold/next, lock and blackout; pong retains its existing wire signature.
- Tiny synthetic 64×48 H.264 fixture at a 2560×1440 Retina drawable: about 59 FPS.
  This does not establish larger-media throughput or Pi performance.

The independent whole-branch review found three important issues. Each was reproduced
before repair and covered by regression tests:

1. Preserve a committed clip's pending EOF while replacement prepares or fails.
2. Invalid clip/folder requests supersede previously pending selections.
3. Coalesce overdue frames and upload at most one per maintenance pass.

All three fixes passed the full suites and sanitizers. No critical or deferred minor
findings were reported; the reviewer declined no in-scope judgments.

Implementation rulings:

- Added `VideoWorker::reject` so failed GPU uploads release candidate decoders on
  the worker and let the committed decoder resume.
- A shared buffer credit travels with each worker frame to enforce the three-frame
  limit even while the presenter owns a polled result.
- Reused the existing low-level GL program, vertex-array and state guards; video
  owns its textures and unpack-state guard. No Fluid solver or render targets are shared.
  The low-level helpers retain their existing `fluid_gl` namespace.
- Fixture generation uses explicit display-rotation and setparams metadata with
  the installed FFmpeg CLI; ffprobe verified the tags.
- Live OSC assertions allow the existing one-second status publication interval.

Actual FFmpeg 5.1 compilation, Raspberry Pi/GLES performance, sustained thermals and
physical target-media playback remain unverified. See `docs/video.md` for the device
acceptance checklist. No push or merge was performed.
