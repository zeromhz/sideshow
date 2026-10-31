# Sideshow TouchOSC controller

Open `Sideshow.tosc` in current [TouchOSC](https://hexler.net/touchosc) for Mac (not TouchOSC Mk1). TouchOSC is commercial and offers a trial. Use Cmd+E to switch between editor and control mode.

In Connections → OSC → Connection 1, enable UDP, set Host to the Pi’s IP address, Send port to 9000 (or its configured OSC port), and Receive port to 9001. These connection settings belong to TouchOSC, not the layout. Local test settings such as 127.0.0.1 and 19000 must be replaced. Allow TouchOSC local network access if macOS asks.

Use All for a single Pi, or select its panel ID. Target selection filters messages; it does not change the destination IP. For several panels use your network’s subnet broadcast address as Host. Ping requests panel IDs, current presets, and FPS; Sideshow replies to the sender’s source port. The controller does not synchronize slider positions with device state.

## Command acknowledgements

Set the layout’s **Reply port** field (tap for the built-in keyboard) to the same port as TouchOSC Connection 1’s **Receive port**, normally 9001. Editing the layout field does not change TouchOSC’s connection settings. Commands require a player with `/sideshow/request` support; older players will not execute these envelopes. Ping/pong remains unchanged and does not prove the acknowledgement reply port is configured correctly.

Each command sends `/sideshow/request` with a unique string request ID, integer reply port, string full command address, and the command’s original typed arguments. The player sends `/sideshow/ack` with four strings: request ID, panel ID, status, detail. The latest action’s replies appear below the controls, one row per panel:

- **applied** confirms the effective value in the detail.
- **accepted** means an asynchronous request was handled, not that the operation completed.
- **rejected** reports a failure; **superseded** means a newer coalesced request replaced this one.
- **No reply after 2s** is unconfirmed: the command may have run despite a lost UDP reply. It is not automatically retried.

Broadcast replies are always labeled partial / all unconfirmed, even if several panels report applied. Up to eight panel replies per command are shown; the controller cannot know that every intended panel responded. Fluid shortcuts keep mode and preset outcomes side by side. The latest action replaces the previous displayed action; out-of-order replies for older actions cannot overwrite it. Unknown, duplicate, wrong-connection, wrong-target, and late replies are ignored. Changing the target or reply port clears correlation state. Pending correlation is bounded to 128 requests and two seconds.

## Fluid persistence

The slider beside Video controls sends `/sideshow/<target>/fluid_persistence`, with a float from 1 to 10. Left is 1× (normal); right is 10× (longest fading). Try 3–4× for lingering colors. Motion and rendering frequency stay unchanged. Continued splashes can make colors accumulate at high persistence.

The Pi must run the player version containing this command. Older builds ignore it. Set `persistence = 4.0` inside the existing `[fluid]` section of `/etc/sideshow/config.toml` and restart `sideshow-player` to make 4× the boot default. Runtime OSC changes do not save to disk.

## Fractals panel

**Fractals panel**, at the bottom of the ISF card, opens a panel over the control cards. The target, Ping, and command replies stay visible. Each shader button sends `mode isf` and then `preset fractal/<name>.fs`, and **Rotate all** sends `mode isf` and then `playlist fractal`. Both outcomes are shown side by side, as with the Fluid shortcuts. The Speed (0–4), Zoom (0.5–4), Color (0–1), and Detail (0–1) sliders send `isf_param` floats to the selected shader, at most about 29 messages per second each. Like the other sliders, they show local values, not panel state. Use the ISF parameter fields for `spin`, `weave`, `power`, `density`, and `reactivity`. **Close** returns to the main controls. The panel needs a player with the fractal shaders installed (`setup/install_isf_examples.sh`).

## Other controls

Fluid shortcuts select Fluid mode and a style, including Pompeii (electric blue, violet, and magenta). Lock stops automatic style rotation; it does not freeze animation. Set `locked = true` in the Pi config’s `[presets]` section to start locked; Unlock resumes rotation. Video Pause affects video only. Blackout hides output while rendering continues. ISF controls require an active shader and its input names; select the correct numeric type, especially integer/bool.

Click a text field to open the built-in onscreen keyboard; Done saves locally, then Load/Set/Send applies it. Physical keyboard entry is not implemented. Nothing is sent merely by opening the layout. Sliders send while moved, at most about 29 messages per second each.

## Build and validate

From the repository root:

```sh
python3 controllers/touchosc/build_layout.py
lua controllers/touchosc/test_controller.lua
```

The generator requires only Python’s standard library. The layout was opened in TouchOSC 1.5.3; automated Lua tests cover request envelopes, typed arguments, correlation, duplicate/late/out-of-order replies, partial broadcasts, timeout reporting, both Fluid shortcut outcomes, Fractals panel shortcuts and slider coalescing, reply-port editing, persistence coalescing, and target changes. The expanded acknowledgement UI and the Fractals panel have not yet been visually validated in TouchOSC. Player tests cover config, command validation, mode retention, and actual GPU dye fading. Raspberry Pi performance and live network behavior still need verification on the device.
