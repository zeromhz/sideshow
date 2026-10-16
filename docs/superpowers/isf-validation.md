# ISF validation — 2026-10-16

Validated on an Apple Silicon Mac running macOS 26.7.1, using SDL desktop OpenGL
4.1 (Metal driver). These results are not Raspberry Pi performance measurements.

- Full player build: successful. CTest: 113/113. Python CLI: 13/13.
- Real GL suites: Fluid, Video, ISF context, ISF runtime and ISF backend passed.
- Core ASan/UBSan: 101/101. ISF runtime and backend ASan/UBSan passed with
  `UBSAN_OPTIONS=halt_on_error=1`.
- Fresh core-only configure/build/tests: 101/101; link lines contain core/Catch2
  only, with no GL/VVISF binary library.
- Modern GLES header syntax checks: all 15 selected vendor translation units
  plus runtime, presenter and backend passed. This is not a Linux link/run test.
- Example installation run twice preserves a modified user shader.
- Opt-in real OSC test passed startup, typed controls/events, malformed payloads,
  failed shader replacement, blackout commands and four cycles through all other
  modes, restoring the remembered ISF shader after each return.

The live test requested a 1280×720 window; Retina produced a **2560×1440 drawable**,
with ISF capped at **1280×720** internally. Plasma pong samples after successive
switch cycles were 59.41, 58.88, 58.47 and 58.76 FPS. Process RSS samples were
112784, 116512, 118608 and 120416 KiB. This short run shows successful repeated
switching, not a proven long-term memory plateau. Texture-budget tests verify
that runtime destruction releases all tracked reservations. Driver allocations
and caches are outside that accounting.

Real pixel tests cover alpha over black/aspect bars, FLOAT target precision,
explicit timing, first-frame events (including after preparation), feedback reset,
imports and paired vertex source, extreme dimensions, sine FFT/silence, shader
compile failure, budget rejection, dirty GL state and teardown. All three bundled
examples are rendered; spectrum responds to synthetic audio and feedback resets
on the visible event frame. Pong does not validate pixels or parameter values.

An early context test aborted because it omitted VVISF's default vertex source
and did not catch VVISF's non-standard exception type. The test now supplies that
source and catches the error; the host runtime translates VVISF errors. Sanitizers
also exposed and verified fixes for an empty budget lease and upstream zero-height
backing division. Live OSC exposed a Video→ISF return failure: a deleted current
program was restored by a state guard. A focused prepared-frame test failed before
program teardown was fixed and passes afterward.

## Repeat locally

Configure with `SIDESHOW_BUILD_PLAYER`, `SIDESHOW_BUILD_GL_TESTS`,
`SIDESHOW_BUILD_VIDEO_TESTS`, and `SIDESHOW_BUILD_ISF_TESTS` ON, then build and run
CTest and `python3 -m unittest discover -s tools`. Run each real GL executable
explicitly (they require a display) and
`python3 tools/test_isf_osc_integration.py --run-live`.

## Outstanding hardware acceptance

On Raspberry Pi 5 / Bookworm, verify actual EGL/GLES compilation and linking,
float framebuffer capability, all bundled shaders, real mic loss/reconnect,
blackout/mode switching, compile latency, sustained FPS/thermals and long-running
RSS. GPU shader compilation cannot be preempted; use trusted shaders. See
[operator limits](../isf.md) for exact file, image and texture resource ceilings.
