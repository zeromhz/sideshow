# Sideshow TouchOSC controller

Open `Sideshow.tosc` in current [TouchOSC](https://hexler.net/touchosc) for Mac (not TouchOSC Mk1). TouchOSC is commercial and offers a trial. Use Cmd+E to switch between editor and control mode.

In Connections → OSC → Connection 1, enable UDP, set Host to the Pi’s IP address, Send port to 9000 (or its configured OSC port), and Receive port to 9001. These connection settings belong to TouchOSC, not the layout. Local test settings such as 127.0.0.1 and 19000 must be replaced. Allow TouchOSC local network access if macOS asks.

Use All for a single Pi, or select its panel ID. Target selection filters messages; it does not change the destination IP. For several panels use your network’s subnet broadcast address as Host. Ping requests panel IDs, current presets, and FPS; Sideshow replies to the sender’s source port. The controller does not synchronize slider positions with device state or acknowledge commands.

## Fluid persistence

The slider beside Video controls sends `/sideshow/<target>/fluid_persistence`, with a float from 1 to 10. Left is 1× (normal); right is 10× (longest fading). Try 3–4× for lingering colors. Motion and rendering frequency stay unchanged. Continued splashes can make colors accumulate at high persistence.

The Pi must run the player version containing this command. Older builds ignore it. Set `persistence = 4.0` inside the existing `[fluid]` section of `/etc/sideshow/config.toml` and restart `sideshow-player` to make 4× the boot default. Runtime OSC changes do not save to disk.

## Other controls

Fluid shortcuts select Fluid mode and a style. Lock stops automatic style rotation; it does not freeze animation. Set `locked = true` in the Pi config’s `[presets]` section to start locked; Unlock resumes rotation. Video Pause affects video only. Blackout hides output while rendering continues. ISF controls require an active shader and its input names; select the correct numeric type, especially integer/bool.

Click a text field to open the built-in onscreen keyboard; Done saves locally, then Load/Set/Send applies it. Physical keyboard entry is not implemented. Nothing is sent merely by opening the layout. Sliders send while moved, at most about 29 messages per second each.

## Build and validate

From the repository root:

```sh
python3 controllers/touchosc/build_layout.py
lua controllers/touchosc/test_controller.lua
```

The generator requires only Python’s standard library. The layout was opened in TouchOSC 1.5.3; automated Lua tests cover persistence packets, coalescing, and target changes. Player tests cover config, command validation, mode retention, and actual GPU dye fading. Raspberry Pi performance and live network behavior still need verification on the device.
