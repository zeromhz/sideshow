# Selectable ISF generator mode

Date: 2026-10-16
Status: Written specification for review; implementation has not started.

## Intent and approved approach

Add ISF as a standalone stage-visual mode alongside MilkDrop, Fluid and Video.
Operators select local shaders and adjust their published parameters over OSC.
The first release supports generators and microphone-reactive visuals. The user
explicitly deferred processing the video library through ISF effects.

The approved approach integrates VVISF-GL into the existing native renderer instead
of implementing another ISF interpreter. The app remains C++17 with SDL, desktop
OpenGL on macOS and GLES 3/KMS on Raspberry Pi 5. Shared output brightness and blackout
remain applied once by main. No browser, second window or audio output is introduced.

Success means local generators, including multipass/audio examples, can be selected
and controlled reliably without disrupting the running visual when a load fails.
Compatibility is measured against the declared feature set below and actual test
shaders, not a claim that every shader on isf.video runs on every GPU.

## Dependencies and integration decision

Use the BSD-3-Clause VVISF/VVGL implementation from mrRay/VVISF-GL. The upstream
revision inspected for this specification is
`7306b1abdd0b5536f0f230c0519453e79184e08b`. Pin the integrated revision and all required
transitive dependencies; preserve their license notices and document local patches.
Its JSON and expression dependencies must be inventoried rather than assumed covered
by the top-level license. Reuse the upstream ISF parser, shader construction and
multipass behavior behind a small application adapter.

The upstream context API can wrap existing CGL/EGL contexts. Its Raspberry Pi sample
uses legacy `/opt/vc` and `bcm_host` build assumptions; it is not a drop-in Pi 5 build.
Provide CMake integration and context adoption for our current SDL-created context.
Do not replace SDL, install Qt/GLFW, or add legacy Broadcom display ownership.

Implementation begins by proving context adoption, ordinary rendering and persistent
passes on the available desktop. Inspect and adapt the GLES path for current headers;
record actual Pi execution as unverified until hardware is available. If the library
cannot meet the context, resource or compatibility requirements with a small documented
adapter/patch, stop and report the concrete incompatibility before changing runtimes or
silently reducing support. No product dependencies are installed during specification.

## Library and selection

- Default root `/opt/sideshow/isf`; development root `deps/isf`.
- Discover `.fs` files recursively, case-insensitive extension and case-sensitive ID.
  Example ID: `audio/spectrum.fs`. An optional sibling with the same basename and
  `.vs` extension supplies a custom vertex shader; it is not independently selectable.
- Ignore hidden entries and symlinks. Relative IDs reject absolute paths, traversal,
  embedded NULs, URLs and ambiguous separators. Revalidate at file open, including
  directory components, using the same defensive approach as the video library.
- A folder collection contains only its direct shader children. `playlist .` is the
  root collection. Selecting a shader selects its parent folder. No reserved `default`
  folder is required.
- Scan and read files off the rendering thread. Cache the process-local catalog and
  source/asset snapshots for preparation. Restart to discover changed or added files;
  live editing/rescanning is outside this release.
- Manual next/previous use sorted relative IDs with wraparound. Random selects another
  eligible shader where possible. Duration-driven changes use shared shuffle settings;
  lock suppresses automatic changes but permits manual selection.
- Startup, folder selection and automatic navigation may try each eligible candidate
  once. A failed explicit preset request does not select an unrelated shader. Failed
  attempts do not spin every frame; another explicit request can retry.

Provisioning installs the bundled examples without replacing user files. Their stable
IDs are `examples/plasma.fs`, `examples/spectrum.fs`, and `examples/feedback.fs`.
Create original, redistributable samples with explicit notices: a procedural generator,
a microphone spectrum, and a persistent-buffer example with an event/reset control.
Do not download an unreviewed community shader collection or treat credits as a license.

## Supported ISF behavior

Target ISF 2.0 generator semantics, including:

- Declared scalar, boolean, integer/menu, point, color and event inputs.
- `audio` and `audioFFT` inputs supplied from the existing capture stream.
- Standard time, frame, date, render-size and pass-index values and ISF image helpers.
- Ordered multiple passes, named temporary targets, persistent buffers, float targets
  where the GPU supports the required format, and pass-size expressions.
- Optional paired vertex shaders and static imported PNG/JPEG textures.

Pass buffers and source textures are owned by one shader instance. Never sample a
texture while simultaneously rendering to it. Persistent state starts cleared and
resets on explicit shader load and mode return. Resize rebuilds size-dependent targets
transactionally and clears persistent history; it retains parameters and shader time.
Zero-size resize events retain the last valid allocation until a positive size returns.

ISF 1.x files, including an absent version marker, are handled through the pinned
library's legacy conversion when supported and tested; malformed or unsupported
versions receive a diagnostic. Desktop-only GLSL features are not automatically portable
to GLES. A compile failure must identify the shader and preserve the current visual.

Shaders requiring dynamic `image` inputs, video effects, and transitions are rejected
with a clear unsupported-input diagnostic. Static `IMPORTED` textures are supported.
Import paths resolve relative to the shader; `../shared/texture.png` is allowed only
when canonical resolution and component checks keep the file within the configured
library. Symlinks and external/network paths remain forbidden. Supply validated,
owned asset data to the runtime; do not allow a library image loader to bypass checks.

Use the library's expression grammar, evaluate buffer sizes against current dimensions
and inputs, and validate finite positive sizes before any allocation. Unsupported float
render targets fail the candidate; never silently substitute lower-precision storage
for a shader requesting float buffers. Feature/format tests must cover the actual GPU
capabilities rather than assuming desktop and Pi equivalence.

## OSC controls and parameter semantics

All commands retain `/sideshow/all/...` and `/sideshow/<panel-id>/...` addressing.
Existing commands and pong's `string,string,float` signature stay compatible.

| Command | Payload | Behavior |
|---|---|---|
| `mode` | string `isf` | Prepare and switch after a successful first render |
| `preset` | relative shader ID | Load from defaults and reset time/history |
| `playlist` | relative folder or `.` | First usable direct child |
| `next`, `prev`, `random` | none | Navigate the selected folder |
| `isf_param` | input name followed by values | Set a declared non-event parameter |
| `isf_event` | input name only | Trigger an event for one presented frame |
| `duration`, `lock` | existing payloads | Shared automatic-selection behavior |
| `master`, `blackout` | existing payloads | Shared final output; animation continues |

`isf_param` uses these exact value shapes:

- `float`: one OSC float or integer, converted to a finite float.
- `long`: one OSC int32; for a declared menu, require membership in its `VALUES`.
- `bool`: one OSC int32 equal to 0 or 1.
- `point2D`: two finite numeric values, in the shader's declared coordinate units.
- `color`: four finite numeric values in RGBA order.

Clamp continuous values to valid declared MIN/MAX bounds where present; absent bounds
add no artificial normalized range. Reject invalid arity, types, non-finite values,
unknown input names and malformed metadata without partial updates. Do not permit
OSC to assign textures, raw audio or reserved runtime uniforms. Events use
`isf_event`, never a persistent boolean assignment.

Examples:

```sh
python3 tools/osc_send.py mode isf
python3 tools/osc_send.py preset 'examples/plasma.fs'
python3 tools/osc_send.py isf_param speed 0.5
python3 tools/osc_send.py isf_param tint 1.0 0.25 0.1 1.0
python3 tools/osc_send.py isf_event reset
```

Extend the Python CLI to support multiple typed values without changing existing
commands. Lexical integers encode as OSC integers; decimal/exponent forms encode as
floats. Parameter type/range validation occurs against the selected shader metadata
in the player. Test actual packet round trips.

Defaults initialize every successful load, including selecting the same shader and
returning to ISF from another mode. Missing defaults follow the pinned runtime's
ISF default rules, captured in tests and operator documentation. Overrides are not
persisted to disk and are not remembered per shader. The last shader/folder is remembered
for the process lifetime. Repeating `mode isf` while already active does not reload.

Commands immediately after `mode isf` or a shader selection target that pending
candidate. Before its metadata is available, hold at most 64 parameter/event commands
in order; overflow discards new commands with a warning. Reject unknown parameters
when metadata arrives. New selections discard controls for superseded candidates.
Repeated events before presentation coalesce per input; an event remains true for
all passes of exactly one presented frame, then clears. Preparation/validation renders
must not consume a pending event before the operator can see it.

ISF-specific commands outside active or pending ISF log a warning and do nothing.
An invalid newest shader/folder request supersedes older pending requests while
retaining the committed shader, following the video review regression requirements.
Pong reports `isf/<relative-id>` only after activation.

## Microphone input

Use the existing capture device, sample rate, disconnect detection and silence fallback.
ISF produces no audio. Maintain a bounded rolling stereo sample history independently
of capture callback chunk size. Duplicate mono into two channels when applicable.

Provide waveform textures with channels as rows and samples as columns; encode zero
amplitude at 0.5. Provide frequency-magnitude textures in ascending frequency order,
one row per channel, without inventing beat events for ordinary ISF parameters.
Default waveform width and spectrum width are 512. Honor a positive integer input
MAX up to 4096 columns; reject larger requests during validation. For waveform widths greater than one, downsampling averages signed samples over
each source interval and encodes `0.5 + 0.5 * mean`, with source samples clamped to
[-1, 1]. A one-column waveform encodes level as `0.5 + 0.5 * RMS` over the window.
Use the most recent history, zero-pad unavailable samples, and test silence, DC and
sinusoidal signals. These are explicit host aggregation conventions.

Use a Hann-windowed real FFT with a bounded power-of-two window sufficient for the
requested spectrum width (up to 8192 samples), normalized magnitudes and no logarithmic
remapping unless the shader implements it. Test expected frequency bins and channel
separation. Prefer an existing appropriately licensed FFT implementation or a small
well-tested bounded implementation; record the choice in the implementation plan.

Waveform silence is 0.5 and FFT silence is zero. A missing/disconnected/stalled input
must not leave stale energy visible. Normal callback gaps must not cause flicker.
Use float sampling textures when available; check precision capabilities explicitly.
Pending first-frame rendering receives the same fresh audio snapshot as the active
backend, without consuming capture data twice or invoking audio processing in callbacks.
`beat_sensitivity` remains stored for other visualizers; ISF audio gain is not implicitly
changed by that unrelated control. Shader-specific gain inputs use `isf_param`.

## Preparation, rendering and ownership

Compose an ISF catalog/source loader, parameter model, audio adapter and runtime adapter
behind `IsfBackend`. Extend mode-indexed storage safely and add a narrow optional ISF
control interface instead of ISF methods on all renderers. Keep catalog, validation,
parameter commands and audio transforms independently testable without GL.

An application-owned bounded loader handles scanning, source reads and image decoding.
Use latest-request generations, cancellation and bounded results; no detached worker
per command, filesystem scan or worker join on the render path. Release CPU loading
resources off-thread; join at application shutdown. All GL use stays on the main
thread and within the existing context.

The controller retains the active backend while loading. Compile, allocate and render
a candidate successfully before committing mode/status/selection. During an ISF-to-ISF
replacement, keep the current shader running until commit; a failure retains its
parameters, time, picture and selection. Clear pending state after failure and avoid
automatic retry loops. Commit and control routing must not depend on OSC sender sleeps.

Shader compilation and individual GPU operations cannot be safely preempted. Preparation
is staged to avoid doing a catalog's worth of compiles in one frame, but the first
release cannot promise stutter-free switching or enforce a hard GPU timeout. Shaders
are operator-installed local programs, not sandboxed untrusted code. Report this limit
honestly; do not add a watchdog that claims it can interrupt arbitrary driver work.

Render into bounded offscreen targets and present the final image fitted to the drawable,
composited over black. Treat shader output as straight RGBA and composite RGB multiplied by clamped alpha
over black in the presentation pass. Final output is opaque; main applies brightness/blackout once.
Establish and restore GL state explicitly across candidate work, including texture/sampler
bindings, pixel-store/PBO state, framebuffers, viewport, VAO and blend/depth/scissor masks.
Return all resources on failure. Avoid a broad refactor of the existing renderers.

TIME starts at zero on a new load, TIMEDELTA is zero on its first frame, and FRAMEINDEX
increments once per complete shader frame. Subsequent time uses finite monotonic elapsed
seconds; all passes see the same frame time. Blackout does not stop the clock. DATE uses
local wall-clock calendar values. There is no new ISF pause/speed command in this release.
Automatic duration starts on activation, not when loading began, and performs at most
one transition after a long stall.

Startup defaults to MilkDrop. If configured to start in ISF, initialize the existing
MilkDrop fallback and prepare ISF; an empty/broken library leaves fallback running.
A recoverable shader/parameter/resize failure retains the last valid state. A fatal
ISF rendering failure makes one MilkDrop recovery attempt; failure exits through the
existing systemd recovery path. Do not change projectM preset-recovery semantics.

## Resource ceilings and configuration

Initial application limits, checked before handing allocations to the runtime:

- Fragment and optional vertex source: at most 1 MiB each; at most 64 declared inputs.
- At most 8 passes and 16 imported images per shader.
- Imported image dimensions at most 4096 per axis and 8,847,360 pixels each; validate
  dimensions and decoded-byte arithmetic before allocation, not just file extension.
- Render output defaults to an aspect-preserving 1280×720 landscape / 720×1280 portrait
  box; each individual target must also fit GL_MAX_TEXTURE_SIZE and 4096 per axis.
- Account for every allocated pass texture, persistent ping-pong pair, audio/imported
  texture and final target: 128 MiB per shader instance, 256 MiB across current and
  candidate ISF instances. Bound runtime buffer pools and release inactive resources.
- Validate pass-size expressions initially and after parameter/size changes. A proposed
  update exceeding limits is rejected atomically while the old instance remains valid.

These limits bound application-managed storage, not total driver memory or shader
execution time. If an upstream allocation path cannot be checked, add a narrow audited
hook/patch before claiming the ceiling is enforced. Do not rely on allocation failure
as ordinary budget validation.

```toml
[visuals]
mode = "milkdrop" # milkdrop | fluid | video | isf

[isf]
root = "/opt/sideshow/isf"
preset = "examples/plasma.fs" # empty selects first usable catalog entry
max_width = 1280
max_height = 720
```

The max dimensions describe a landscape box and swap for portrait drawables; never
upscale the offscreen render beyond the drawable. Accept positive integer values up
to 4096, subject to memory/GPU ceilings. Invalid config keys follow existing per-key
warnings and defaults. Development config uses `deps/isf`. No network connection is
required during playback; no runtime shader downloads are introduced.

## Validation and acceptance

1. Pure tests: nested/exact IDs, folder order, safe imports and paired vertex files,
   malformed metadata, supported input defaults/types/ranges, strict OSC arity, event
   pulses, pending-control bounds, supersession and per-mode memory.
2. Audio tests: silence/midpoint, channel separation, known sine frequencies, MAX sizes,
   window normalization, capture chunk independence, stalls and reconnects.
3. Real library/GL tests: ordinary generators, paired vertex shader, imported image,
   audio waveform/FFT, multipass feedback, float target capability handling, pass-size
   expressions and resource ceilings. Compile errors retain the running shader.
4. Controller/GL tests: pending mode+shader+parameter commands, unknown newest selection,
   events on first visible frame, repeated selection/default reset, bounded automatic
   fallback, resize failures, contaminated GL state, blackout, resource cleanup and
   switching to/from MilkDrop, Fluid and Video.
5. Full desktop build and existing C++/Python regressions, core and applicable adapter
   sanitizers, fresh core-only build without VVISF/GL, and real localhost OSC round trips.
   Test linked upstream code and actual rendering, not a mock ISF interpreter.
6. Pi checklist: build with current Bookworm GLES headers; test all bundled examples,
   target shaders/resolutions, microphone reconnects, command latency during compile,
   sustained memory, thermal behavior and switching under blackout. Label unavailable
   hardware checks and shader-specific compatibility limits explicitly.

Record the pinned dependency inventory, patch rationale, example provenance, OSC command
reference, supported feature matrix and observed performance in operator/developer docs.
An independent whole-branch review is required before integration. Preserve the user's
native execution and isolated-worktree preference for the later implementation plan.
Do not merge or push the implementation without a request for that feature.

## References

- ISF overview: https://isf.video/
- JSON, input and imported-image contract: https://docs.isf.video/ref_json.html
- Runtime uniforms: https://docs.isf.video/ref_variables.html
- Shader image helpers: https://docs.isf.video/ref_functions.html
- Multipass/persistent targets: https://docs.isf.video/ref_multipass.html
- Audio guidance: https://docs.isf.video/primer_chapter_8.html
- Runtime and license: https://github.com/mrRay/VVISF-GL
- Context adapter API: https://github.com/mrRay/VVISF-GL/blob/7306b1abdd0b5536f0f230c0519453e79184e08b/VVGL/include/GLContext.hpp
- Legacy Pi build evidence: https://github.com/mrRay/VVISF-GL/blob/7306b1abdd0b5536f0f230c0519453e79184e08b/examples/raspbian/Makefile
