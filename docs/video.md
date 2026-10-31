# Local video playback

Video is a separate, always-silent mode alongside MilkDrop and Fluid. Copy clips into
`/opt/sideshow/videos` on each device (development: `deps/videos`). Nested folders
are supported. Keep files and directories readable by the service user. Provisioning
creates the root without deleting existing media. Restart the player after adding files;
the library is scanned once per process, in the background.

```sh
sudo mkdir -p '/opt/sideshow/videos/concert'
sudo cp intro.mp4 '/opt/sideshow/videos/concert/intro.mp4'
python3 tools/osc_send.py mode video
python3 tools/osc_send.py preset 'concert/intro.mp4'
python3 tools/osc_send.py video_end loop
python3 tools/osc_send.py video_pause 1
python3 tools/osc_send.py video_restart
python3 tools/osc_send.py video_pause 0
```

Clip IDs are exact, case-sensitive relative paths including their extension. Hidden
files/directories and symlinks are excluded. Absolute paths, traversal, URLs and
external media references are rejected. Extensions are case-insensitive: mp4, m4v,
mov, mkv, webm and avi. Actual codec support depends on the linked FFmpeg build.

| OSC command | Value | Behavior |
|---|---|---|
| `mode` | `video` | Retain current visuals until a video frame has uploaded |
| `preset` | `concert/intro.mp4` | Restart that clip; select its parent folder |
| `playlist` | `concert` or `.` | First usable direct child; nested folders are independent |
| `next` / `prev` | none | Sorted folder order, wrapping |
| `random` | none | Another clip in the folder, or restart a singleton |
| `video_end` | `loop` / `hold` / `next` | Loop by default, hold last frame, or advance within folder |
| `video_pause` | integer `0` / `1` | Resume / freeze video clock |
| `video_restart` | none | Restart, retaining pause and end policy |
| `lock` | existing toggle | End-next loops the clip; manual navigation still works |
| `master` / `blackout` | existing values | Shared output controls; blackout does not pause playback |

Video ignores shared duration, shuffle, hardcut and beat sensitivity. End policy and
selection are remembered across mode changes; returning starts at time zero unpaused.
Changing hold to loop/next acts immediately at EOF, or after resume if paused. Commands
sent immediately after `mode video` target the pending video. Video-specific controls
outside active/pending video log a warning and do nothing. Pong retains its existing
wire format and reports `video/concert/intro.mp4` only after activation.

A failed explicit clip load preserves the last picture and selection. Startup, folder
selection and automatic navigation try eligible clips once, then stop. Missing startup
media keeps MilkDrop running. In loop mode (and next mode while locked) the decoder rewinds
on its own when it reaches the end and keeps decoding ahead on one continuous timeline, so the
loop point does not stall as long as decoding keeps up with real time. The loop length is the
last frame's end time. Restart (`video_restart`) still begins the clip again from scratch.
Audio tracks are never decoded or played.

```toml
[visuals]
mode = "milkdrop" # milkdrop | fluid | video
[video]
root = "/opt/sideshow/videos"
preset = ""      # first usable clip, recursively sorted
end = "loop"
```

## Media and resource limits

Use progressive SDR H.264 MP4 at 720p or 1080p as a starting point. HDR and interlaced
media are rejected. Source dimensions must be at most 4096 per axis and 8,847,360
pixels. Frames larger than a 1920×1080 landscape or 1080×1920 portrait box are scaled down to
fit it. Frames reach the GPU as 8-bit planar YUV 4:2:0: 8-bit 4:2:0 footage within the box is
copied as is, and other formats are converted to it once on the CPU. The shader converts to RGB
using the clip's BT.601/709/2020 matrix and limited or full range (unspecified matrices use
BT.709 from 720 lines up, BT.601 below). Presentation honors sample aspect and 90-degree
rotation metadata, fits on black, and uses opaque alpha.

One application-owned worker performs scanning and software decoding. It owns at most a
current and candidate decoder; three completed frames are allowed across worker and
presenter ownership. Three codec threads and five-second cooperative operation deadlines
bound work. Cancellation cannot preempt arbitrary codec code or a blocked filesystem call.
The render thread never waits for decoding. Late frames leave the prior image visible.

FFmpeg 5.1+ development libraries are required for the player. Core-only tests need no
FFmpeg or GPU. macOS setup installs Homebrew `ffmpeg`; Pi setup installs libavformat-dev,
libavcodec-dev, libavutil-dev and libswscale-dev. No codec binaries are vendored. FFmpeg
licensing and enabled codecs vary with the installed build; inspect `ffmpeg -buildconf`
and comply with that build's LGPL/GPL and codec licensing terms when redistributing.

## Validation and target checklist

Synthetic fixtures and their generation commands live in `tests/fixtures/video`.
Enable decoder tests with `-DSIDESHOW_BUILD_VIDEO_TESTS=ON`; GPU tests additionally need
`-DSIDESHOW_BUILD_PLAYER=ON -DSIDESHOW_BUILD_GL_TESTS=ON` and a real SDL GL context.
Run `ctest --test-dir build --output-on-failure`, `./build/sideshow_video_gl_tests` and
`./build/sideshow_gl_tests`. Sanitizer builds cover core and decoder/worker behavior.

Pi hardware performance and compilation against actual FFmpeg 5.1 headers are unverified.
Before deployment, verify on the device:

- Target H.264 720p/1080p codec profiles, rotation, aspect and frame pacing.
- Long loops, VFR files, late frames, memory usage and thermal throttling.
- Pause/restart, every end policy, lock, and rapid mode changes under blackout.
- Missing/deleted/corrupt clips, unreadable folders and fallback behavior.
- Command latency while opening large or damaged local clips.

ISF shader mode remains the subsequent feature and is not part of this video implementation.

Decoder resource admission checks both the source pixel count and each axis before
allocating decoded frame buffers, including mid-stream resolution changes. Probing
uses demuxer metadata and parsers with internal probe decoders disabled; the selected
video decoder then runs with the guarded allocator. FFmpeg may log a probe codec
whitelist warning for this intentional restriction. Files whose dimensions cannot
be discovered without probe decoding are rejected; normal decoding errors retain
the current picture as described above.
