# Sideshow

Autonomous, audio-reactive stage visuals for 6–8 panels, each driven by a Raspberry Pi 5
running [projectM](https://github.com/projectM-visualizer/projectm) (MilkDrop presets).
Each Pi listens to its own USB mic, boots straight into visuals, and takes live control over OSC.

## Project documentation

- [Architecture](docs/architecture.md): source map, runtime flow, threading, and recovery behavior.
- [Development](docs/development.md): prerequisites, builds, tests, and where to make changes.
- [Configuration reference](docs/configuration.md): defaults, accepted ranges, and runtime overrides.

This README covers installation and live operation. The [original design](docs/superpowers/specs/2026-09-22-sideshow-player-design.md)
and [implementation plan](docs/superpowers/plans/2026-09-22-sideshow-player.md) preserve the earlier
planning context; the guides above describe the current implementation.

## Raspberry Pi setup (per panel)

1. Flash **Raspberry Pi OS Lite (64-bit, Bookworm)** with Raspberry Pi Imager. In its settings, set
   the username/password, enable SSH, and enter the stage Wi-Fi.
2. Plug in the USB mic and HDMI, boot, then SSH in:
   ```bash
   sudo apt-get install -y git
   git clone https://github.com/zeromhz/sideshow.git ~/sideshow
   sudo ~/sideshow/setup/install.sh --panel-id panel-01
   sudo reboot
   ```
   Use a unique id per panel (`panel-01` … `panel-08`); it becomes the hostname
   (`ssh <user>@panel-01.local`) and the panel's OSC address.
3. After reboot the panel goes black → visuals in about 20 s. No keyboard, mouse, or network needed.

To update: `cd ~/sideshow && git pull && sudo setup/install.sh && sudo systemctl restart sideshow-player` (keeps your `/etc/sideshow/config.toml`).

Useful commands:

```bash
journalctl -u sideshow-player -f          # live logs
sudo systemctl restart sideshow-player    # restart the player
sudo nano /etc/sideshow/config.toml       # settings (restart to apply)
```

Notes:
- Resolution is automatic: the player uses the screen's native mode, capped at 2560×1440
  (a 4K screen steps down to 1440p or 1080p), preferring 60 Hz. The chosen mode is logged
  (`screen native …, using …`). Change `max_width`/`max_height`, or set `width`/`height`, in
  the config to override.
- If no screen is detected at boot, the player exits and systemd retries every 2 s, so visuals
  appear shortly after the screen is powered on.
- The tty1 login prompt is disabled; use SSH.

## OSC control

UDP port 9000. Address one panel as `/sideshow/<panel-id>/<command>` or all panels as
`/sideshow/all/<command>`. Send to a panel's IP, or to the subnet broadcast address (e.g.
`192.168.1.255`) to reach every panel with one message.

| Command | Args | Effect |
|---|---|---|
| `next` / `prev` / `random` | – | Change preset (soft blend) |
| `lock` | int 0/1 | Freeze the current preset |
| `playlist` | string | Switch playlist (e.g. `default`, `fractal`, `hypnotic`) |
| `preset` | string | Load a preset by filename (`.milk` optional) |
| `blackout` | int 0/1 | Instant black (overrides master) |
| `master` | float 0–1 | Output brightness (smoothed, curved) |
| `duration` | float s | Seconds per preset (1–3600) |
| `beat_sensitivity` | float | 0–2 |
| `hardcut` | int 0/1 | Beat-triggered hard cuts (MilkDrop only) |
| `mode` | string | `milkdrop` or `fluid`; cut to the selected visualization |

`/sideshow/ping` → every panel replies `/sideshow/pong <panel-id> <preset> <fps>` to the sender.
Ints and floats are interchangeable for toggles and numbers.

Under heavy OSC traffic, pending master/blackout updates keep only their latest value and take
priority over other commands. The ordinary queue holds 128 commands; additional ordinary
commands are dropped while it is full. Processing is bounded per frame to keep visuals moving,
though an individual preset load can still take longer than a frame.

CLI (no dependencies):

```bash
tools/osc_send.py ping                          # list panels on the network
tools/osc_send.py next                          # all panels (broadcast)
tools/osc_send.py --panel panel-03 blackout 1   # one panel
tools/osc_send.py --host 192.168.1.42 master 0.5
```

The default `--host 255.255.255.255` broadcast may only leave the primary interface on a Mac with
several networks, so for the stage network pass its subnet broadcast, e.g. `--host 192.168.1.255`.

### VDMX master fader

In VDMX, add an OSC output to the subnet broadcast address (e.g. `192.168.1.255`), port `9000`.
Map the fader (the one your MIDI fader controls) to the address `/sideshow/all/master`, with the
output range 0.0–1.0 as a float. If VDMX won't send to a broadcast address, add one output per
panel IP with the same OSC address.

The panels apply `master_curve = 2.0` so the fade looks even. If you shape the curve in VDMX
instead, set `master_curve = 1.0` in each panel's config.

## Playlists

Each directory (or symlink) under `/opt/sideshow/presets` is a playlist. The install creates
`default` (the full [projectM-presets-rpi5](https://github.com/mickabrig7/projectM-presets-rpi5)
pack, 7,998 presets — a set filtered by the pack author to those that averaged ≥24 fps on a Pi 5
with projectMSDL's benchmark) plus one playlist per sub-pack: `dancer`, `drawing`, `fractal`,
`geometric`, `hypnotic`, `particles`, `reaction`, `sparkle`, `supernova`, `waveform` (the
"creamofthecrop" categories), `isosceles` (isoscelesmashup), `bltc`, and `en-d`. To make a
curated playlist, create a directory and symlink or copy `.milk` files into it, then restart the
player.

The pack's ≥24 fps filter was measured on the author's own Pi 5 at an unstated resolution, so
treat it as a starting point, not a guarantee — measure actual fps at bring-up with
`tools/osc_send.py ping`, and expect a curated `stage` playlist of the fastest, most reliable
presets to follow once that's done.

## Development (macOS)

```bash
setup/dev-macos.sh                                  # one time: brew deps, projectM, presets
cmake -S . -B build -DSIDESHOW_BUILD_PLAYER=ON -DCMAKE_PREFIX_PATH="$PWD/deps/install"
cmake --build build -j
./build/sideshow_tests                              # unit tests
python3 -m unittest discover -s tools               # OSC CLI tests
./build/sideshow-player --config setup/config.dev.toml
tools/osc_send.py --host 127.0.0.1 ping
```

Set `SIDESHOW_DEBUG=1` to log ignored OSC messages.

## On-hardware smoke checklist

Run this on each new panel, and after any significant change:

- [ ] Power on with no keyboard or network: visuals appear within ~20 s, with no boot text or cursor
- [ ] Visuals react to clapping and music near the mic
- [ ] `tools/osc_send.py ping` lists the panel at ~60 fps
- [ ] Every OSC command in the table works, both via `--panel <id>` and via broadcast `all`
- [ ] The VDMX fader fades smoothly to black and back, with no visible stepping
- [ ] Unplug the mic: visuals keep running (on silence). Replug: reaction returns within ~3 s
- [ ] `sudo pkill -9 sideshow-player`: visuals return within ~3 s
- [ ] Pull the power and restore it: visuals come back on their own
- [ ] Boot with the screen off, then turn it on: visuals show within a few seconds, at the right resolution
- [ ] The log line `screen native …, using …` shows the expected mode

## Fluid visualization

Fluid is a native OpenGL adaptation of [Pavel Dobryakov's fluid simulation](https://github.com/PavelDoGreat/WebGL-Fluid-Simulation), with eleven looks adapted from [Chris Pirillo's Oilwheel](https://arcade.pirillo.com/oilwheel.html). It runs without a browser or internet connection, moves in silence, and adds splashes in response to microphone onsets.

```bash
python3 tools/osc_send.py mode fluid
python3 tools/osc_send.py preset lava
python3 tools/osc_send.py --panel panel-03 mode milkdrop
```

Available fluid presets: `light-show`, `lava`, `aurora`, `nebula`, `ember`, `neon`, `oil-slick`, `ink`, `frost`, `wild`, `overhead`. Display names are also accepted, case-insensitively. Overhead inverts the output for a light background.

Each panel runs one mode. Switching cuts immediately and remembers each mode's last selection for this process; returning restarts its animation. Brightness, blackout, duration, lock, and beat sensitivity remain in effect. Fluid supports the built-in `default` playlist only. Preset changes update the ongoing field, so old dye dissipates into the new palette. `hardcut` affects MilkDrop only.

MilkDrop remains the default. See [configuration](docs/configuration.md#fluid-settings) for startup mode and quality settings, and [development](docs/development.md#fluid-rendering-validation) for validation limits.

Additional Pi acceptance checks:

- [ ] Every fluid preset displays its palette and effects, including Overhead inversion.
- [ ] Quiet-room ambient motion continues; microphone onsets add bounded splashes.
- [ ] Mic unplug/replug leaves visuals running without repeated stale audio bursts.
- [ ] Blackout stays black across repeated mode changes and resizing.
- [ ] Float framebuffer support and manual-filter fallback work on the target GLES driver.
- [ ] Sustained rendering and switching meet the show's FPS and thermal requirements.

Desktop validation does not establish Raspberry Pi performance; Pi/GLES hardware validation is still required.

## Video mode

See [local video playback](docs/video.md) for OSC controls, recursive folder semantics,
configuration, FFmpeg dependencies, architecture limits and hardware validation.
