# Configuration reference

The player reads `/etc/sideshow/config.toml`, or the path passed with `--config PATH`, once
at startup. Restart to apply edits. OSC changes affect only the running process and are not
saved. Relative paths resolve from the process working directory, not the config directory.

Missing keys silently retain defaults. A missing file or malformed TOML uses all defaults
and logs a warning. Wrong types and invalid values retain that key's default with a warning;
unknown keys are ignored. Defaults and validators live in `src/config.h` and `src/config.cpp`.
The [example config](../setup/config.example.toml) is a copyable starting point.

## Identity and display

| Key | Default | Accepted values / behavior |
|---|---|---|
| `panel_id` (top level) | `"panel-01"` | 1–32 lowercase ASCII letters, digits, or dashes; cannot start with a dash |
| `display.width` | `0` | Integer 0–7680; set both dimensions above zero for an explicit fullscreen mode |
| `display.height` | `0` | Integer 0–4320 |
| `display.max_width` | `2560` | Integer 320–7680; automatic fullscreen selection cap |
| `display.max_height` | `1440` | Integer 240–4320; automatic fullscreen selection cap |
| `display.fps` | `60` | Integer 1–240; requested projectM FPS and loop pacing |
| `display.fullscreen` | `true` | Boolean; false creates a resizable development window |

An unsupported explicit fullscreen size falls back to automatic selection. Automatic
selection favors native resolution within the caps, otherwise aspect-matching modes within
the caps, then larger area and suitable refresh rate. If no mode fits, it uses native
resolution even above the caps. Windowed mode uses each positive dimension as supplied,
otherwise 1280×720 defaults; caps do not apply. FPS is a target, not a guarantee.

## Audio and network

| Key | Default | Accepted values / behavior |
|---|---|---|
| `audio.device` | `""` | First capture device if empty; otherwise case-sensitive substring of its SDL name |
| `audio.sample_rate` | `48000` | Integer 8000–192000 Hz |
| `osc.port` | `9000` | Integer 1–65535; UDP listener port |

Capture requests stereo float samples; SDL handles device format conversion. A panel ID
filters OSC addresses; it does not select a network interface. Changing `panel_id` in TOML
alone does not change the machine hostname.

## Presets

| Key | Default | Accepted values / behavior |
|---|---|---|
| `presets.root` | `"/opt/sideshow/presets"` | Nonempty path; each visible child directory/symlink is a playlist |
| `presets.textures` | `"/opt/sideshow/textures"` | Nonempty texture search path |
| `presets.playlist` | `"default"` | Nonempty starting playlist name; missing/empty playlist falls back to `default` |
| `presets.duration` | `30.0` | 1–3600 seconds per preset |
| `presets.soft_cut_duration` | `3.0` | 0–60 seconds of blending |
| `presets.hardcut_enabled` | `false` | Boolean; enables beat-triggered hard cuts |
| `presets.beat_sensitivity` | `1.0` | 0–2 |
| `presets.shuffle` | `true` | Boolean; random order when true, sequential when false |
| `presets.locked` | `false` | Boolean; start with automatic preset rotation locked; OSC Unlock resumes |

Nonempty paths are accepted without checking existence during config parsing. The catalog
scans at startup; restart after adding presets or playlists.

## Output

| Key | Default | Accepted values / behavior |
|---|---|---|
| `output.master_default` | `1.0` | 0–1 initial master level |
| `output.master_curve` | `2.0` | 0.1–10 exponent; 1 means linear brightness |
| `output.master_smoothing_ms` | `50.0` | 0–5000 ms exponential time constant; 0 disables smoothing |

Brightness is the smoothed master raised to the curve exponent. Blackout overrides it
immediately without changing the master target. Disabling blackout reveals the current
smoothed master level. See [OSC control](../README.md#osc-control) for runtime overrides.

## Fluid settings

```toml
[visuals]
mode = "milkdrop" # milkdrop | fluid | video | isf

[fluid]
preset = "light-show"
simulation_resolution = 128 # integer 32–256, shorter dimension
dye_resolution = 512       # integer 128–1024, shorter dimension
ambient_interval = 2.5      # 0.1–60 seconds
persistence = 1.0           # 1–10; higher values prolong dye fading
```

Quality settings are fixed for the run. Targets preserve drawable aspect ratio, independently of HDMI resolution. Invalid values warn and use defaults. Higher quality requires more GPU memory and time; the defaults are a starting point for Pi benchmarking.

Built-in slugs: `light-show`, `lava`, `aurora`, `nebula`, `ember`, `neon`, `oil-slick`, `ink`, `frost`, `wild`, `overhead`. `preset` also accepts case-insensitive display names. Fluid has only the `default` playlist; unknown playlists or presets leave the selection unchanged. `presets.duration`, `presets.shuffle`, and `presets.beat_sensitivity` initialize MilkDrop and Fluid.

OSC `mode fluid` / `mode milkdrop` switches modes at a frame boundary. The last preset and playlist of each mode are remembered until exit; animation restarts when returning. These changes do not rewrite TOML. Duration, lock, beat sensitivity, master, and blackout are shared. Hardcut is retained for MilkDrop only. Fluid preset changes affect the ongoing field without a full-image crossfade.

A failed switch keeps the active backend. Fluid startup failure falls back to MilkDrop. A fatal fluid rendering failure attempts one MilkDrop recovery, then exits nonzero if recovery fails so systemd can restart. Pong keeps its `string,string,float` signature and reports `fluid/<slug>` in its preset field.

## Video mode

See [local video playback](video.md) for OSC controls, recursive folder semantics,
configuration, FFmpeg dependencies, architecture limits and hardware validation.

## ISF settings

Set `[visuals] mode = "isf"` for ISF startup. `[isf]` accepts `root` (default
`/opt/sideshow/isf`), `preset` (default `examples/plasma.fs`, otherwise exact relative `.fs` ID),
`max_width` (1280) and `max_height` (720). Dimensions must be integers 1–4096;
invalid values warn and retain defaults. Portrait swaps the caps. Development
setup uses `deps/isf` and `examples/plasma.fs`. Restart after config or catalog
changes. See [ISF controls, defaults and limits](isf.md).

Fluid persistence scales each style’s dye lifetime without changing its motion, splash timing, preset rotation, or rendering frequency. `1` preserves the original look; `4` gives approximately four times longer fading. OSC `/sideshow/<panel-id-or-all>/fluid_persistence` accepts one finite number from 1 to 10; invalid values are ignored. Runtime changes survive style/mode switches but reset to the TOML value on restart.

To boot on a fixed Fluid style, set `locked = true` in the existing `[presets]` section and select `[fluid] preset`. The animation continues and manual preset selection still works. Lock persists across mode changes; TouchOSC Lock/Unlock overrides it until restart. Config changes require restarting the player.
