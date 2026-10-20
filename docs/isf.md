# ISF generators

ISF is a fourth, independently selectable mode alongside MilkDrop, Fluid and Video.
It runs local ISF 1/2 fragment shaders, optional paired vertex shaders, persistent
multipass feedback and static PNG/JPEG imports. Microphone waveform and FFT inputs
are supported. Video effects, transitions and dynamic image inputs are not supported.

Setup installs three original MIT-licensed examples, preserving existing files:
`examples/plasma.fs` (speed and tint), `examples/spectrum.fs` (microphone spectrum
and gain), and `examples/feedback.fs` (persistent trails, decay and reset event).
It also installs three fractal generators: `fractal/julia.fs`, `fractal/dive.fs` and
`fractal/kleinian.fs`. Their parameters are listed in the [README](../README.md#fractals).

```sh
python3 tools/osc_send.py --host 127.0.0.1 mode isf
python3 tools/osc_send.py --host 127.0.0.1 preset examples/plasma.fs
python3 tools/osc_send.py --host 127.0.0.1 isf_param speed 1.5
python3 tools/osc_send.py --host 127.0.0.1 isf_param tint 0.2 0.6 1.0 1.0
python3 tools/osc_send.py --host 127.0.0.1 preset examples/feedback.fs
python3 tools/osc_send.py --host 127.0.0.1 isf_event reset
```

## Library and selection

The default root is `/opt/sideshow/isf` (`deps/isf` in the development config).
Discovery is recursive; `.fs` extensions are matched case-insensitively. IDs include
the extension and path relative to the root and are case-sensitive. A matching `.vs`
file supplies the vertex shader. Hidden entries and symlinks are excluded. Restart
to discover additions and removals. Files are read again when selected.

`playlist examples` selects that folder's direct shaders, excluding subfolders;
`playlist .` selects root shaders. `next`, `prev`, `random`, `duration` and `lock`
operate within the selected folder. Explicit selection works while locked.
Startup tries the configured shader then eligible alternatives; an empty or broken
library retains the startup MilkDrop fallback. A failed replacement leaves the
current shader running. Selecting a shader again resets its defaults, time and
history. Returning from another mode remembers its ID but restarts it; repeating
`mode isf` while already active does not reload it.

## OSC controls

Use `/sideshow/<panel-id>/...` or `/sideshow/all/...` on the configured UDP port.

| Command | OSC arguments | Behavior |
|---|---|---|
| `mode` | string `isf` | Prepare and switch after a successful first frame |
| `preset` | string relative ID | Load an exact shader ID |
| `playlist` | string folder | Select direct children of a folder |
| `isf_param` | string name + 1, 2 or 4 numbers | Set scalar, point or RGBA input atomically |
| `isf_event` | string name | Latch event through one successfully presented frame |

Numbers must be finite OSC int32 or float32. Booleans require OSC int32 0 or 1, integer inputs
require OSC int32 values, and menu values must appear in `VALUES`. Continuous inputs
clamp only to declared bounds; undeclared bounds are not invented. Wrong names,
types or arity leave values unchanged. CLI `2` sends an integer and `2.0` a float.
Events apply to every pass of their frame. Controls during preparation follow the
newest selection, with at most 64 queued; superseding it discards its controls. After a rejected selection, parameters and
events remain rejected until a valid shader selection succeeds.
Outside active/pending ISF mode they are ignored with a warning.

Declared defaults reset on each load. Missing defaults follow the pinned runtime:
float 0.5, bool true, long 0, point (0,0), color (0,0,0,0).
`master` and `blackout` apply after rendering, once, across all modes. ISF output
is aspect-fitted, with alpha composited over black. Pong IDs start with `isf/`.

## Microphone and rendering

`audio` and `audioFFT` inputs receive two rows (left and right) of float textures;
mono capture is duplicated. Audio width defaults to 512; integer `MAX` selects
1–4096 columns. Waveform uses up to 8192 recent stereo frames, resampled to width;
a one-column waveform reports RMS. FFT uses a periodic Hann window and a radix-2
transform sized to the next power of two at least twice the number of bins.
Capture loss/stalls clear both active and pending histories to silence. Spectrum
therefore stays dark without a microphone; the other examples and the fractals
animate in silence.

The fractals keep their animation phases and audio envelopes in a small persistent
RGBA8 target, with each value packed into 32-bit fixed point. They use no float render
target, so they need no float-framebuffer extension. Integrating the phase is what keeps
speed changes from jumping. The dive avoids running out of single-precision depth in two
ways. It loops over a self-similar region around a Misiurewicz point, so it never goes
deeper than about 4e-6. It also iterates every pixel as a perturbation of that point's
exact orbit, with rebasing, which keeps it sharp at that scale.

`[isf] max_width = 1280` and `max_height = 720` cap internal resolution while
preserving aspect ratio; caps swap for portrait. Each must be an integer 1–4096.
The actual GPU texture limit also applies. Floating pass targets are never silently
downgraded; unsupported formats reject the shader.

## Limits and trust

Shaders are executable GPU programs: use trusted shaders. File reading and image
decoding run on one cancellable worker, but shader compilation/rendering runs on
the GL thread and cannot be preempted. A pathological shader can stall the driver;
resource limits do not provide a GPU timeout or a general sandbox.

Source files are limited to 1 MiB each, 64 inputs, 8 passes and 16 static imports.
Imports must resolve inside the shader root (including `../shared`), with no
symlink traversal. PNG/JPEG imports allow at most 32 MiB encoded, 4096 pixels per
axis and 8,847,360 pixels per image; total decoded imports are capped at 128 MiB.
Tracked texture storage, including pooled and overlapping replacement textures,
is capped at 128 MiB per runtime and 256 MiB globally. Driver/compiler overhead,
framebuffers and non-texture allocations are additional process memory.

The renderer uses pinned [VVISF-GL](https://github.com/mrRay/VVISF-GL) with local
context, import and allocation patches, plus vendored stb_image. See
`third_party/isf` and `third_party/stb` for revisions, patches and licenses.
See [validation](superpowers/isf-validation.md) for measured desktop results and
outstanding Raspberry Pi checks.
