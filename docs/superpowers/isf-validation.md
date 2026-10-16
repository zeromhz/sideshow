# ISF validation — 2026-10-16

Validated on an Apple Silicon Mac running macOS 26.7.1, using SDL desktop OpenGL
4.1 (Metal driver). These results are not Raspberry Pi performance measurements.

- Full player build: successful. CTest: 117/117. Python CLI: 13/13.
- Real GL suites: Fluid, Video, ISF context, ISF runtime and ISF backend passed.
- Core ASan/UBSan: 105/105. ISF runtime and backend ASan/UBSan passed with
  `UBSAN_OPTIONS=halt_on_error=1`.
- Fresh core-only configure/build/tests: 105/105; link lines contain core/Catch2
  only, with no GL/VVISF binary library.
- Modern GLES header syntax checks: all 15 selected vendor translation units
  plus runtime, presenter and backend passed. This is not a Linux link/run test.
- Example installation run twice preserves a modified user shader.
- Opt-in real OSC test passed startup, typed controls/events, malformed payloads,
  failed shader replacement, blackout commands and four cycles through all other
  modes, restoring the remembered ISF shader after each return.

The live test requested a 1280×720 window; Retina produced a **2560×1440 drawable**,
with ISF capped at **1280×720** internally. Plasma pong samples after successive
switch cycles were 58.82, 58.65, 58.65 and 58.71 FPS. Process RSS samples were
112544, 116912, 119184 and 120800 KiB. This short run shows successful repeated
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

## Independent review and fixes

One independent whole-branch review found two important issues. Both received
failing regressions and fixes, followed by the full 113-case suite, 13 Python
cases, all five GL suites, runtime/backend ASan+UBSan, adapter GLES syntax checks
and the four-cycle live OSC test. No minor findings were deferred.

- Resize/parameter admission now allocates the complete required frame storage,
  including ping-pong, old output overlap and frame-zero redraw peaks, before
  committing. Unused pool storage can be reclaimed to admit a valid update.
  Staged state retains a rollback snapshot until rendering succeeds; output
  dimensions track the last successful image. Recoverable frame rejection keeps
  the previous parameters, history and picture without advancing its clock.
  Tests cover the 20,000-byte resize reproducer, production-limit FLOAT target
  growth, rejected frame-zero feedback redraw and delayed aspect-ratio commit.
- An invalid newest preset/folder leaves a rejected control target. Later
  parameters/events cannot mutate the old shader; a successful selection restores
  control. Regressions cover both parameters and events for presets and folders.

Additional memory probes before these final fixes: 12 mixed-mode cycles rose from
112272 to 134512 KiB RSS; 20 shader-only cycles rose from 98288 to 102112 KiB.
These are process-level observations, not an attributed ISF leak or a plateau.
Long-running memory attribution remains an acceptance limitation.

Review scope decisions: Pi execution remains unverified without hardware (possible
platform-specific failures); memory stability remains unproven rather than inferred
from texture accounting (possible retained CPU/driver allocations); vendored stb
bulk was excluded from the independent audit while its decoding boundary was
reviewed (upstream implementation defects could remain). No claim of full third-party
source audit or production hardware acceptance is made.

## Follow-up review fixes

The requested follow-up review found that a named final FLOAT pass used RGBA8
output, despite intermediate FLOAT passes working. A regression storing 2.0 in
persistent final-pass history failed before the fix and passes afterward. Final
output now uses that target's declared format and evaluated dimensions; admission
uses its existing reserved destination. A second GL regression verifies its aspect
ratio at presentation.

Selection, fallback, generation checks, direct-folder navigation, duration and
bounded control ownership now live in `IsfPlayback` within `sideshow_core`.
Four pure tests cover retries, supersession, invalid newest targets, queue limits,
ordered mixed parameters/events, atomic rejection, lock and automatic navigation.
Immediate and queued commands both use `IsfParameters::apply`. The configuration
reference now correctly documents `examples/plasma.fs` as the default.

Post-fix validation: 117/117 CTest, 13/13 Python, 105/105 core-only and core
ASan/UBSan, runtime/backend ASan/UBSan, all five native GL executables, current
adapter GLES syntax checks and four-cycle live OSC all pass. The focused independent
review found no blocking issues; its suggested mixed-control test was added.
Live plasma samples were 59.00, 59.17, 58.76 and 59.52 FPS. Pi and long-running
memory acceptance limitations above remain unchanged.
