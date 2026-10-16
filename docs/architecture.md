# Architecture

Sideshow runs one C++17 process, `sideshow-player`, per Raspberry Pi 5. Each panel
runs one selectable mode: MilkDrop, Fluid, local Video, or ISF generators.
A local microphone drives audio-reactive modes.
OSC provides live control, but rendering does not require a network connection.
Broadcast commands can address multiple panels; there is no shared clock or synchronized
preset playback. Camera input, a web UI, direct MIDI input, and persistence of OSC changes
are not implemented.

## Source map

| Files | Responsibility |
|---|---|
| `src/main.cpp` | Startup, object lifetimes, event loop, command dispatch, frame pacing, shutdown |
| `src/config.*` | TOML loading, defaults, validation, warnings |
| `src/display.*`, `src/display_mode.*` | SDL window and GL context; fullscreen mode selection |
| `src/audio_capture.*`, `src/audio_ring.*`, `src/audio_feed.h` | Device selection/reconnection, buffered stereo float PCM, and silence fallback |
| `src/renderer.*`, `src/brightness_pass.*` | projectM rendering and final brightness multiplication |
| `src/output_level.*` | Master smoothing, brightness curve, blackout |
| `src/preset_catalog.*` | Filesystem scan, filename lookup, session blacklist |
| `src/preset_manager.*` | projectM playlist operations and deferred failure recovery |
| `src/osc_parse.*`, `src/command.h` | Validated command values and bounded command processing |
| `src/osc_server.*` | liblo UDP listener and ping replies |
| `setup/` | Dependency builds, preset downloads, configuration, Pi provisioning/service |
| `tools/osc_send.py` | Standard-library Python OSC command-line client |

`sideshow_core` contains logic that can be tested without SDL, projectM, audio devices,
or a GPU. The optional `sideshow-player` target adds those platform integrations.

## Runtime and ownership

Startup loads configuration, initializes SDL video/events and attempts audio initialization,
then creates the display, renderer, preset catalog/manager, output state, audio buffer/capture,
and OSC server. The declaration order in `main.cpp` ensures these are destroyed in reverse:
the OSC server and capture stop before their shared data disappears, and projectM is destroyed
before the GL context. SDL quits afterward. SIGINT, SIGTERM, and SDL quit events stop the loop.

Audio and OSC exchange data with the main loop; Video and ISF also own CPU workers:

```text
SDL audio callback --> AudioRing ----> main loop --> active backend --> brightness pass --> display
liblo OSC thread ---> CommandQueue -> main loop
liblo OSC thread <--- PlayerStatus <-- main loop (roughly once per second)
```

Only the main thread calls projectM or OpenGL and changes preset/output state.
The audio ring, command queue, and status snapshot use mutexes. AudioRing holds about
250 ms of interleaved stereo samples; overflow drops the oldest samples to limit latency.
The command queue holds up to 128 ordinary commands in FIFO order, discarding new ordinary
commands when full. Blackout and master each have a separate latest-value slot, served before
ordinary commands (blackout first). Repeated pending output updates are coalesced. Each frame
processes at most eight commands and stops after a command exhausts a two-millisecond budget.
A single projectM preset load cannot be interrupted, so this bounds additional queued work,
not the duration of that load.

Each frame processes SDL events, handles a pending preset failure, applies queued commands,
retries audio capture if needed, feeds fresh buffered PCM to active and pending backends, updates output brightness,
renders, swaps buffers, and periodically publishes preset/FPS status. Vsync is requested;
the loop also delays according to the configured FPS budget.

## Rendering and output

The Pi service selects SDL KMS/DRM video and ALSA audio without a desktop. Non-Apple
builds use OpenGL ES 3; macOS uses OpenGL 3.3 core. Fullscreen selection considers the
native mode, configured caps, aspect ratio, and refresh rate. Explicit supported dimensions
take precedence. If no mode fits the caps, the implementation falls back to the native mode.

The active backend renders to the default framebuffer. A fullscreen triangle then multiplies its
color by `pow(smoothed_master, master_curve)`. Master uses exponential smoothing with
the configured time constant. Blackout immediately forces brightness to zero while the
master continues smoothing and projectM continues rendering.

## Presets and recovery

At startup, `PresetCatalog` scans visible directories/symlinks directly under the preset
root. Each becomes a playlist, with `.milk` files found recursively and sorted. Lookup by
filename is case-insensitive and accepts an omitted extension. Duplicate basenames resolve
to one indexed file, so unique filenames are preferable for direct selection.

Failed preset basenames are excluded for the rest of the process, including matching names
in other playlists. Callbacks record failures and automatic switch requests; `tick()` handles
them outside the callbacks to avoid recursive preset switching. Navigation commands can also
complete pending recovery. Failed-item cleanup is tracked separately from the need to advance,
so a later successful selection cancels recovery without retaining broken entries. The manager
adjusts its playlist cursor as entries are removed, preserving manual and automatic navigation;
timed and beat-driven requests retain their soft/hard-cut intent. A non-default playlist falls back to `default`
when exhausted or after ten consecutive failures. If no usable playlist exists at startup,
projectM's idle preset remains; exhausting playback can leave the current picture displayed.
Restart to rescan files and clear the blacklist.

Missing capture devices are retried every three seconds; device removal closes capture
and allows reconnection while rendering continues. `AudioFeed` replaces projectM's retained
waveform with zero samples while capture is unavailable, or after 250 ms without new samples
from an open capture device. Normal gaps between capture callbacks preserve the last waveform.
The pinned projectM version retains 576 audio frames but reports a 480-frame submission limit;
the silence fallback flushes the complete waveform in multiple submissions.
If SDL audio initialization itself fails,
the process renders without capture and does not retry subsystem initialization.

Failure to create the OSC listener disables remote control without stopping visuals.
Display initialization errors and other caught startup/runtime exceptions exit with failure.
On the Pi, systemd restarts the process after two seconds without a restart limit. Recovery
timings and real preset performance still need the [hardware smoke checklist](../README.md#on-hardware-smoke-checklist).

## Control boundary

OSC messages targeting this panel or `all` are parsed into command variants; invalid messages
are ignored, with diagnostics when `SIDESHOW_DEBUG=1`. Numeric commands are clamped to their
supported ranges. Ping replies read the latest status snapshot, not projectM directly.
OSC control is unauthenticated UDP; use a trusted stage network. See the
[command table](../README.md#osc-control) for addresses and argument types.

## Selectable visual backends

`VisualController` owns an active `VisualBackend` and an optional pending candidate, shared playback settings, and the last selection of each mode. Video prepares on an application-owned decoding worker and commits after its first texture upload; existing backends prepare synchronously. A candidate is fully initialized before replacing the active backend; failure leaves the old backend alive. Inactive GPU resources are released after a successful switch. Main applies `OutputLevel` through one shared `BrightnessPass` after backend rendering.

`MilkdropBackend` owns Renderer → Catalog → PresetManager in declaration order, ensuring callback teardown precedes projectM destruction. `FluidBackend` composes pure `FluidPlayback`/`FluidMotion` with `FluidRenderer`. The native solver uses ping-pong float targets for velocity, dye, and pressure, followed by optional bloom/sunrays and display shading. `fluid_gl` owns resources and probes renderable formats and interpolation. Shader provenance and MIT notice are in `third_party/fluid`.

Only the main thread touches either backend. The existing bounded command queue and `AudioFeed` remain in the loop. Fluid limits audio onset splashes to three per frame with a 120 ms cooldown and ambient splashes to one per frame. Its simulation timestep is capped at 1/60 second; long gaps never create catch-up loops.

## Video mode

See [local video playback](video.md) for OSC controls, recursive folder semantics,
configuration, FFmpeg dependencies, architecture limits and hardware validation.

## ISF mode

`isf_catalog`, `isf_document` and `isf_files` discover and validate sources with
root-relative, no-follow file access. `isf_loader` owns one CPU worker and passes
immutable snapshots tagged by session/generation; retired snapshots are released
on the worker. `isf_images` decodes bounded PNG/JPEG imports. `isf_parameters`
validates typed controls; `isf_audio`/`isf_fft` maintain waveform and spectrum.

`isf_services` owns the loader, global texture budget and lazy adopted SDL GL
context. `isf_runtime` owns a private VVISF scene/pool, pass textures and leases;
`isf_presenter` fits its output over black before the shared brightness stage.
The SDL context outlives these objects and remains owned by the host. Loader
shutdown joins before service/context destruction. No worker owns GL objects.

`isf_playback` owns selection, navigation, fallback attempts, request generations,
duration and bounded control queues in `sideshow_core`, independently of GL.
`isf_backend` dispatches its requests to the loader and commits a candidate only
after successful first-frame rendering. Immediate and queued controls share
`isf_parameters` validation.
The controller routes selection/ISF controls to a pending mode and feeds fresh
PCM to both active and pending backends. Capture stalls clear both histories.
Generation checks discard superseded results and pending controls. Filesystem
work is asynchronous; shader compilation still runs synchronously on the GL
thread. See [ISF limits](isf.md) and [validation](superpowers/isf-validation.md).
