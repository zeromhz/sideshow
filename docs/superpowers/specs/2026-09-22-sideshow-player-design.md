# Sideshow Player — Design (v1)

**Date:** 2026-09-22
**Status:** Approved in brainstorming, pending spec review

## Context

The band wants 6–8 flat panels on stage, each driven by a Raspberry Pi, showing
audio-reactive visuals autonomously. Long-term goals include camera inputs mixed
into the visuals and multi-panel coordination. This spec covers the first
sub-project only:

> A Raspberry Pi 5 that boots straight into fullscreen, audio-reactive MilkDrop
> visuals (via projectM), reacts to a local USB mic, and accepts OSC control
> over Wi-Fi — including a master fader driven from VDMX.

### Out of scope for v1

- Camera input (the architecture leaves a seam for it; no camera code)
- Cross-panel preset synchronization
- Web UI
- Persisting OSC-made changes back to config
- MIDI input (VDMX handles MIDI and speaks OSC to the panels)
- Scaled rendering (render below output resolution and upscale): added only if bring-up shows performance problems

## Decisions

| Topic | Decision |
|---|---|
| Hardware | Raspberry Pi 5, HDMI to each panel |
| Audio input | One USB mic per Pi (room sound; each panel independent) |
| Control | Autonomous by default; OSC over Wi-Fi for live control |
| Fader source | VDMX sends native OSC (VDMX itself is driven by a MIDI fader) |
| Approach | Custom C++ player linking libprojectM v4 (not projectMSDL, not Butterchurn) |
| OS | Raspberry Pi OS Lite (Bookworm, 64-bit), no desktop |
| Language / build | C++17, CMake |
| Dependencies | libprojectM v4 (+ its playlist library, built from a pinned release tag with GLES enabled), SDL2, liblo, toml++ (header-only) |

## Architecture

One process, `sideshow-player`, per Pi.

| Unit | Responsibility | Depends on |
|---|---|---|
| **Config** | Load and validate `/etc/sideshow/config.toml` into a plain struct | toml++ |
| **Display** | SDL2 window + OpenGL ES 3 context. On the Pi: KMS/DRM fullscreen, no X/Wayland. On macOS: a normal window. Picks the screen mode at startup (see Display resolution). Vsync on. | SDL2, DisplayMode picker |
| **DisplayMode picker** | Pure logic: given the screen's mode list, its native mode, an optional explicit size, and a size cap, choose the mode to use. | none |
| **AudioCapture** | SDL2 audio capture on its own callback thread; writes float PCM into a lock-free/mutexed ring buffer. Detects device loss and retries. | SDL2 |
| **Renderer** | Owns the projectM instance. Each frame: drain ring buffer into `projectm_pcm_add_float`, render projectM (4.1.7 always draws to the default framebuffer), then a multiply-blend fullscreen pass that applies output brightness (master × blackout). | Display, AudioCapture, PresetManager, OutputLevel |
| **PresetManager** | Wraps projectM's playlist library: playlist directories, shuffle, timed auto-advance, beat-triggered hard cuts, lock, next/prev/random/load-by-name, skip-and-remember failed presets. | Renderer's projectM handle |
| **OutputLevel** | Pure logic: holds master target, current smoothed value, curve exponent, blackout flag. `update(dt)` slews toward target; `brightness()` returns `pow(smoothed, curve) × (blackout ? 0 : 1)`. | none |
| **OscServer** | liblo UDP server on its own thread. Parses and validates incoming messages into `Command` values and pushes them onto a thread-safe queue. Answers `/sideshow/ping`. | liblo |
| **CommandQueue** | Thread-safe queue of `Command` variants, drained by the main loop between frames. | none |

### Threading rule

Only the main thread touches projectM, OpenGL, PresetManager, and OutputLevel.
The audio thread only writes the ring buffer; the OSC thread only pushes
commands (and reads a small atomically-updated status snapshot to answer pings).

### Data flow

```
USB mic ─▶ AudioCapture (thread) ─▶ ring buffer ─┐
                                                 ▼
OSC ─▶ OscServer (thread) ─▶ CommandQueue ─▶ main loop ─▶ PresetManager / OutputLevel
                                                 │
                                                 ▼
                           projectM ─▶ default framebuffer ─▶ brightness pass (multiply blend) ─▶ HDMI
```

### Camera seam (future)

A post-projectM compositing pass already exists (the brightness pass). Camera
frames can later be composited there, or fed to projectM once a projectM release
lets apps supply a target framebuffer (4.1.7 does not). No camera code in v1.

## OSC interface

- **Transport:** UDP, default port 9000 (configurable).
- **Addressing:** each panel accepts both `/sideshow/all/<cmd>` and
  `/sideshow/<panel-id>/<cmd>`. Senders can unicast to one Pi or send to the
  subnet broadcast address (e.g. `192.168.1.255:9000`) and target by prefix.

| Command | Args | Effect |
|---|---|---|
| `next` | – | Next preset (soft transition) |
| `prev` | – | Previous preset |
| `random` | – | Random preset from current playlist |
| `lock` | int 0/1 | Freeze / unfreeze current preset (disables auto-advance) |
| `playlist` | string | Switch to named playlist (subdirectory of preset root) |
| `preset` | string | Load a specific preset by filename within the preset root |
| `blackout` | int 0/1 | Instant black output; rendering and audio keep running |
| `master` | float 0.0–1.0 | Output brightness target (smoothed, curved) |
| `duration` | float seconds | Auto-advance interval |
| `beat_sensitivity` | float | projectM beat sensitivity, 0–2 |
| `hardcut` | int 0/1 | Enable/disable beat-triggered hard cuts |

Plus the global `/sideshow/ping` (no args): the panel replies to the sender's
address with `/sideshow/pong <panel-id:string> <preset:string> <fps:float>`.

**Validation:** unknown addresses, wrong arg counts, or wrong types are logged
at debug level and ignored. Numeric args are clamped to valid ranges. Unknown
playlist or preset names log a warning and leave current state unchanged. OSC
input can never crash the player.

**Argument type leniency:** int args also accept floats (non-zero = 1) and float
args also accept ints, since OSC senders (VDMX included) vary.

### Master fader behavior

- Target value is clamped to 0–1.
- Smoothed toward target with a ~50 ms time constant (configurable) so 10–30 Hz
  fader updates don't visibly step.
- Displayed brightness = `smoothed ^ master_curve × (blackout ? 0 : 1)`.
  `master_curve` defaults to 2.0 (perceptually even fade); set to 1.0 if the
  curve is shaped in VDMX instead.
- At startup the target and smoothed value equal `master_default` (1.0), so a
  panel that reboots mid-show without a controller is visible, not dark.

### VDMX setup (documented in README)

Point a VDMX OSC output at the subnet broadcast address, port 9000, address
`/sideshow/all/master`, range 0–1 float. If broadcast output isn't usable in
VDMX, configure one output per panel IP — the `all` prefix works either way.

## Display resolution

At startup the player reads the screen's mode list (from its EDID) and its native mode:

1. If `display.width`/`height` are set (> 0) and the screen supports that exact size, use it.
   If not supported, log a warning and fall through to automatic selection.
2. Automatic: use the native size if it fits within `max_width` × `max_height`
   (default 2560×1440). Otherwise use the largest supported mode within the cap,
   preferring the native aspect ratio (so a 4K TV runs at 1440p or 1080p).
3. Among modes of the chosen size, prefer 50–60 Hz (60 over 50). Higher refresh
   rates would make projectM render more frames than needed.
4. If nothing fits the cap, use the native mode and log a warning.

The chosen and native modes are logged. projectM renders at the output resolution.
There is no scaled mode in v1; if bring-up shows low fps, first lower the cap
(e.g. 1920×1080), then consider a scaled render mode as follow-up work.

**No screen detected at boot:** SDL finds no connected display, the player logs
it and exits non-zero, and systemd restarts it every 2 s. The next start after the
screen appears picks up its real resolution. Most screens still report their
resolution in standby, so this is rarely visible. No HDMI mode is forced on the
kernel command line.

## Configuration

`/etc/sideshow/config.toml` (example with defaults):

```toml
panel_id = "panel-01"          # set by install.sh --panel-id

[display]
width = 0                      # 0 = automatic (see Display resolution); >0 = explicit mode
height = 0
max_width = 2560               # cap for automatic selection
max_height = 1440
fps = 60
fullscreen = true              # false for windowed dev on macOS

[audio]
device = ""                    # "" = default capture device; else substring match on SDL device name
sample_rate = 48000

[osc]
port = 9000

[presets]
root = "/opt/sideshow/presets"
textures = "/opt/sideshow/textures"
playlist = "default"           # subdirectory of root
duration = 30.0                # seconds per preset
soft_cut_duration = 3.0
hardcut_enabled = false
beat_sensitivity = 1.0
shuffle = true

[output]
master_default = 1.0
master_curve = 2.0
master_smoothing_ms = 50
```

Missing file or missing keys fall back to defaults. Invalid values log an error
and fall back to the default for that key. The player starts even with a broken
config.

## Boot and provisioning

### Boot

- `sideshow-player.service` (systemd): runs as user `sideshow` (groups
  `video`, `audio`, `render`, `input`), `Restart=always`, `RestartSec=2`.
  Ordered after `sound.target`; does **not** wait for network-online.
- Boot quieting: `quiet`, `logo.nologo`, `vt.global_cursor_default=0`, console
  on tty3, `disable_splash=1` — power-on goes black → visuals.
- Target: visuals within ~20 s of power-on.

### `setup/install.sh`

Idempotent; runs on a fresh Raspberry Pi OS Lite install:

1. Install apt dependencies (build tools, CMake, SDL2 dev, liblo dev, GLES/EGL dev).
2. Clone and build libprojectM at a pinned tag with GLES enabled; install to `/usr/local`.
3. Build and install `sideshow-player` to `/opt/sideshow/bin`.
4. Fetch the pinned preset and texture packs (`setup/fetch_presets.sh`) and create playlist symlinks under `/opt/sideshow/presets`.
5. Write `/etc/sideshow/config.toml` if absent (never overwrites an existing one).
6. Create `sideshow` user, install and enable the systemd unit.
7. `--panel-id panel-NN` sets `panel_id` and the hostname.
8. Apply boot-quieting tweaks and disable the tty1 getty. No HDMI mode is forced (see Display resolution).

Wi-Fi credentials and SSH are configured via Raspberry Pi Imager, not stored in
this repo. A `pi-gen` golden image is a later step, not v1.

### Presets

Presets come from projectM's "Cream of the Crop" pack (≈9,800 presets, already
curated by the projectM team), pinned to a commit. `default` links to the whole
pack; each pack category (fractal, hypnotic, waveform, …) becomes its own
playlist. Custom playlists are extra directories under the preset root.

## Error handling

| Failure | Behavior |
|---|---|
| No screen detected at boot | Log, exit non-zero; systemd restarts every 2 s until a screen appears |
| Mic missing at boot / unplugged mid-show | Keep rendering on silence; retry opening the device every 3 s; log once per state change |
| Preset fails to load | Skip to next, remember as bad for this session, log warning |
| All presets in a playlist fail / playlist empty | Fall back to `default` playlist; if that fails, render projectM's idle preset |
| Bad OSC message | Ignore and log (debug level) |
| OSC port unavailable | Log error, keep rendering (no remote control) |
| Crash | systemd restarts in ~2 s |

Logs go to stdout/stderr → journald (`journalctl -u sideshow-player`).

## Repo layout

```
sideshow/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── config.{h,cpp}
│   ├── display.{h,cpp}
│   ├── display_mode.{h,cpp}
│   ├── audio_capture.{h,cpp}
│   ├── renderer.{h,cpp}
│   ├── preset_manager.{h,cpp}
│   ├── output_level.{h,cpp}
│   ├── osc_server.{h,cpp}
│   └── command.h              # Command variant + CommandQueue
├── tests/                     # unit tests (Catch2 via CMake FetchContent)
├── setup/
│   ├── install.sh
│   ├── build_projectm.sh
│   ├── fetch_presets.sh
│   ├── dev-macos.sh
│   ├── sideshow-player.service
│   └── config.example.toml
├── tools/osc_send.py
└── README.md
```

## Testing

**Unit tests** (run on macOS and on the Pi, no GPU needed):

- Config: defaults, overrides, invalid values fall back per key.
- OSC parsing: both prefixes, wrong panel ID ignored, type leniency, clamping,
  unknown addresses → no command.
- OutputLevel: smoothing converges, curve math, blackout interaction, startup value.
- DisplayMode picker: native within cap, 4K stepping down, aspect preference, 60 Hz preference, nothing fits.
- PresetManager logic against a temporary preset directory (playlist switching,
  missing names, bad-preset skipping) — logic separated from projectM calls
  behind a thin interface so it's testable without a GL context.

**Tools:** `tools/osc_send.py` — send any command to one IP or broadcast
(e.g. `osc_send.py --all master 0.5`); also runs `ping` and prints pongs.

**On-hardware smoke checklist** (README): boots to visuals with no input,
reacts to sound, every OSC command works, VDMX fader fades smoothly, mic unplug
and replug recovers, `kill -9` recovers in ~2 s, power-cycle recovers.

**Dev loop:** builds and runs on macOS (windowed, default mic) for fast
iteration; deploy to Pi for performance validation.

## Success criteria

1. A freshly imaged Pi 5 + `install.sh --panel-id panel-01` + reboot → fullscreen
   audio-reactive visuals with no keyboard, mouse, or network required.
2. Uses the screen's native resolution (capped at 2560×1440) and fps is measured at bring-up; if typical presets run well below 60 fps, lower the cap, then plan a scaled render mode.
3. All OSC commands work from `osc_send.py` and from VDMX, individually and via broadcast.
4. VDMX master fader smoothly fades output to black and back.
5. Survives mic unplug, bad presets, malformed OSC, and process kill without manual intervention.
