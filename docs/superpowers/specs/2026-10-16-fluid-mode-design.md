# Selectable fluid visualization mode

Date: 2026-10-16
Status: Proposed for user review; implementation has not started.

## Intent and confirmed scope

Add Pavel Dobryakov's WebGL Fluid Simulation to Sideshow with default looks
adapted from Oilwheel. The user selected a separate mode alongside MilkDrop:
each panel renders one mode at a time, and different panels can choose different
modes. Simultaneous layering and mixing are outside this change.

The proposed integration preserves Sideshow's Raspberry Pi OS Lite/KMS setup,
macOS development support, local microphone input, autonomous operation, and OSC
control. It must not require Chromium, a desktop, internet access, or a second
microphone capture session at runtime.

## Approach and alternatives

**Recommended: native C++/OpenGL adaptation.** Port the upstream GPU solver and
its shaders into a second native renderer. WebGL's shader operations map to
Sideshow's GLES 3 / desktop GL 3.3 pipeline. This requires porting and visual
validation, but shares the existing display, microphone, output stage, and
service lifecycle.

A browser subprocess could reuse the JavaScript more directly, but would add a
browser/display stack and a control bridge, complicating fullscreen ownership,
microphone recovery, and Pi provisioning. An embedded browser introduces similar
runtime weight and platform dependencies. Neither is proposed for this version.

## Operator behavior

- MilkDrop remains the startup default for existing configurations.
- A new `mode` OSC command accepts `milkdrop` or `fluid` and follows the existing
  panel/all addressing convention.
- Switching mode is a frame-boundary cut, not a crossfade. Master brightness and
  blackout remain in effect across the change.
- The last selected preset and playlist for each mode are remembered for the
  process lifetime. Returning to a mode restarts that look; it does not promise
  preservation of its prior animation or fluid field.
- GPU resources for the inactive mode are released after a successful switch.
  Preparing a candidate mode may temporarily allocate resources for both modes.
- Unsupported modes, unknown presets, and failed initialization log a warning
  and leave the currently working mode selected.
- OSC changes remain transient; startup choices come from TOML.

Examples:

```bash
python3 tools/osc_send.py mode fluid
python3 tools/osc_send.py preset lava
python3 tools/osc_send.py --panel panel-03 mode milkdrop
```

| Existing control | Fluid behavior |
|---|---|
| `next`, `prev`, `random` | Select among the fluid presets |
| `preset` | Accept a stable slug or case-insensitive display name |
| `playlist` | `default` selects the built-in fluid collection; other names leave state unchanged |
| `duration` | Controls automatic fluid preset rotation, using the existing 1–3600 s range |
| `lock` | Stops automatic preset rotation; fluid motion and audio reaction continue |
| `master`, `blackout` | Same shared final output controls as MilkDrop |
| `beat_sensitivity` | Adjusts fluid audio response within the existing 0–2 range; zero disables audio-triggered splashes |
| `hardcut` | MilkDrop-specific; retains its setting for MilkDrop and has no effect on fluid |

Duration, lock, and beat sensitivity are shared live controls. Existing startup
`presets.duration`, `presets.shuffle`, and `presets.beat_sensitivity` also seed
fluid playback. Mode changes do not reset these live settings. Fluid `next` and
`prev` follow catalog order when shuffle is off; shuffled navigation selects a
random preset. A new explicit selection restarts the fluid rotation timer.

Fluid preset changes apply the selected settings to the ongoing field; new dye
uses the new palette while older dye dissipates. Discrete effects such as ink
inversion change immediately. Crossfading complete rendered images is outside
scope. Existing MilkDrop transitions remain unchanged.

Keep `/sideshow/pong`'s existing `string, string, float` signature. The preset
field remains the filename for MilkDrop and becomes `fluid/<slug>` for fluid,
so the current CLI can display mode and preset without a protocol break.

## Configuration

Add optional sections; omitted or invalid values follow the current default and
warning conventions. Existing files remain valid.

```toml
[visuals]
mode = "milkdrop"              # milkdrop | fluid

[fluid]
preset = "light-show"          # slug from the built-in collection
simulation_resolution = 128    # shorter field dimension; integer 32–256
dye_resolution = 512           # shorter dye dimension; integer 128–1024
ambient_interval = 2.5         # seconds between autonomous splashes; 0.1–60
```

Resolution is independent of HDMI output resolution and respects aspect ratio.
The Pi-oriented dye default is lower than the reference web page's 1024 value;
this is a starting point for benchmarking, not a verified frame-rate guarantee.
Quality settings are fixed for a run, so preset changes do not repeatedly
reallocate simulation textures.

## Default preset collection

Ship all eleven named looks as built-in data, with complete values for every
preset rather than applying partial patches that inherit the previous look.
Include palette selection, dye/velocity dissipation, vorticity, splat force and
radius, shading, bloom settings, sunray settings, and ink inversion.

| Display name | Stable slug | Character |
|---|---|---|
| Light Show | `light-show` | Bright spectrum, baseline motion |
| Lava | `lava` | Warm colors, slower lingering dye |
| Aurora | `aurora` | Green/cyan palette and broad flow |
| Nebula | `nebula` | Purple/pink/blue swirls |
| Ember | `ember` | Red/orange, restrained motion and strong glow |
| Neon | `neon` | Bright contrasting colors and energetic motion |
| Oil Slick | `oil-slick` | Teal/pink/gold/purple palette |
| Ink | `ink` | Blue monochrome, no bloom or sunrays |
| Frost | `frost` | Pale blue/cyan, gentle broad flow |
| Wild | `wild` | Fast spectrum changes and strong vorticity |
| Overhead | `overhead` | Inverted ink/projection look, no bloom or sunrays |

Use the referenced numeric settings and palettes, adapted to native rendering.
Do not claim pixel-identical output: input gestures, seeded motion, resolution,
and audio driving differ from the browser experience. Custom fluid preset files
and a preset editor are deferred.

## Runtime boundaries

- **Visualization controller:** owns active mode, shared playback settings,
  remembered per-mode selections, transactional switching, and command routing.
  Main remains responsible for SDL events, capture, bounded commands, and timing.
- **MilkDrop backend:** wraps the existing projectM renderer, catalog, and preset
  manager. Keeps their dependency lifetimes and recovery behavior intact.
- **Fluid playback logic:** GPU-independent catalog, selection/rotation state,
  palette generation, and audio/ambient emission policy, with injectable time
  and random input for deterministic behavioral tests.
- **Fluid renderer:** owns shaders and framebuffer resources for velocity, dye,
  pressure, divergence, curl, bloom, and sunrays. Runs the upstream sequence of
  vorticity, divergence, iterative pressure solve, gradient subtraction, and
  advection; performs the final shaded fluid display pass.
- **Shared output pass:** applies the existing smoothed/curved master and
  blackout exactly once after the active backend draws.

Only the main thread accesses GPU resources or visualization state. Audio capture
continues to write the existing bounded PCM ring; the main thread supplies PCM
to the active backend. Audio analysis must not change projectM's PCM delivery
or the recently added silence/reconnection behavior.

Fluid uses normalized stereo PCM energy, a smoothed baseline, a minimum onset
threshold, and a 120 ms cooldown to trigger bounded splashes. It is inspired by
Oilwheel's response but does not replicate its browser FFT implementation.
At most three audio-triggered splashes and one ambient splash are emitted per
frame; excess accumulated time does not create a catch-up burst. Ambient motion
continues without a microphone. Initialization seeds visible dye immediately.

Advance the solver with a timestep clamped to 1/60 s, matching the upstream cap.
A slow frame must not cause unbounded simulation catch-up. Preserve the existing
bounded OSC processing and output-priority behavior.

## GPU support and failure handling

Use desktop GL 3.3 and GLES 3 shader variants. Probe floating-point framebuffer
renderability and filtering support at runtime; use compatible wider formats
when single/two-channel float targets are unsupported and manual bilinear
advection when needed. Check framebuffer completeness and shader compile/link
results before committing a mode change.

Shader source and preset data ship locally. Resource construction is exception
safe, including partially constructed framebuffers/programs. Restore the GL
state needed by the shared output pass and by a subsequent renderer.

If fluid is unavailable at startup, log the reason and start MilkDrop. If fluid
initialization fails during a live switch, retain the previous renderer and its
selection. If an active fluid backend encounters a detected fatal error, attempt
a single switch to MilkDrop; if recovery itself fails, exit nonzero for the
existing systemd restart policy. Do not retry a broken fluid renderer every frame.

Resizing updates viewport and aspect-dependent targets. Enforce configured size
bounds before allocation. GPU hangs or driver failures that do not return to the
process cannot be solved by application exception handling.

## Source provenance

- Solver and shaders: https://github.com/PavelDoGreat/WebGL-Fluid-Simulation
  (MIT, copyright Pavel Dobryakov). Pin the imported source to a commit and
  retain the license and adaptation notes with the native implementation.
- Preset reference: https://arcade.pirillo.com/oilwheel.html, inspected
  2026-10-16. Attribute the preset settings/palettes to the Oilwheel reference
  and Chris Pirillo; record a content hash of the consulted page. Its engine
  includes Pavel's MIT notice; do not assert that this separately licenses the
  surrounding site, UI, or branding. Adapt the configuration values and build
  Sideshow's own supporting logic instead of importing the website.

No browser UI, analytics, advertisements, fonts, recording/export functionality,
or external runtime assets are included.

## Verification and acceptance

1. Core-only tests cover mode/config validation, OSC parsing, CLI encoding, all
   preset lookups, independent complete preset values, bounded audio/ambient
   emission, timers/lock/shuffle, and switching failure preserving current state.
2. Existing queue, audio, output, catalog, and preset-recovery tests remain green.
3. Full macOS build succeeds. Exercise both modes with a real GL context: each
   preset renders nonempty output, resize works, blackout is black, and repeated
   switches release old resources and retain selected preset/control settings.
   Use a deterministic seed/audio fixture where useful; do not substitute fake
   GPU tests for real shader compilation.
4. Run core tests under address/undefined-behavior sanitizers. Validate malformed
   config/OSC and failure paths without a GPU where possible.
5. On a Pi 5, verify GLES framebuffer formats, all eleven looks, mic unplug/replug,
   quiet-room ambient movement, OSC switching and output controls, and a sustained
   switching/rendering run. Record observed FPS and adjust the recommended quality
   settings based on evidence. If no Pi is available, clearly report this portion
   as unverified rather than declaring the feature stage-ready.

This design does not add simultaneous mixing, mode crossfades, a browser runtime,
mouse painting, new recording features, or synchronized multi-panel simulation.
