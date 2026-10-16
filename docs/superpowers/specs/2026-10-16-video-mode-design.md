# Selectable video playback mode

Date: 2026-10-16
Status: Written specification for user review; implementation has not started.

## Intent and agreed scope

Play local videos on demand through OSC as a separate visualization mode alongside
MilkDrop and Fluid. A configurable library root may contain videos directly and
arbitrarily nested folders. Playback is always silent. End behavior is selectable
through OSC: loop (the default), hold the final frame, or advance within the folder.

The user approved native FFmpeg decoding into the existing OpenGL output pipeline,
relative-path clip selection through `preset`, pause/resume and restart controls,
bounded background decoding, and keeping the current picture when loading fails.
Returning to video mode restarts its remembered clip. Shared master and blackout
controls retain their existing behavior.

Video is the first deliverable. ISF shader support requested from https://isf.video
remains the next separate mode and will receive its own specification and plan after
video. This specification does not silently narrow the future ISF compatibility
scope or introduce video-as-ISF-input behavior.

## Approach

Use FFmpeg's libavformat, libavcodec, libavutil and libswscale to demux, decode and
convert local video frames. A background worker owns FFmpeg contexts; the main
thread uploads completed frames and draws them in the existing GL context. No audio
stream is decoded or sent to an output device. Microphone capture continues for
other modes, but video ignores microphone PCM.

This keeps the Raspberry Pi OS Lite/KMS deployment and macOS development paths
consistent. GStreamer offers another native pipeline but adds pipeline and plugin
management. An external player would complicate display ownership, blackout and
transactional switching; neither alternative is selected.

Initial playback uses software decoding. Hardware acceleration and zero-copy
uploads are future optimizations requiring device validation, not promises of this
release. Use SDR H.264 MP4 at 720p or 1080p as the initial operating recommendation;
measure actual Pi throughput before promising frame rates.

## Library and naming

- Default root: `/opt/sideshow/videos`; development root: `deps/videos`.
- Scan recursively off the rendering thread when video is first requested. Reuse
  the process-local catalog on subsequent visits. Restart to discover added files;
  live filesystem watching and a rescan command are outside this first version.
- Candidate extensions, case-insensitive: `.mp4`, `.m4v`, `.mov`, `.mkv`, `.webm`,
  `.avi`. Extensions identify candidates; the linked FFmpeg build determines
  actual container/codec support. Audio-only files are rejected.
- Stable clip IDs are exact, case-sensitive relative paths with `/` separators,
  including the extension: `concert/intro.mp4`. Do not resolve basename aliases;
  `concert/intro.mp4` and `ambient/intro.mp4` are different clips.
- Ignore hidden entries and symlinks. Accept only regular files under the canonical
  configured root. Reject absolute paths, `..` components, embedded NULs and URL
  inputs; revalidate containment and file type when opening. The library is local,
  read-only input. FFmpeg protocols are limited to local file access; no network
  streams, playlists that fetch external resources, or device input.
- Scan errors are logged per inaccessible entry where traversal can continue. A
  missing, unreadable or empty root does not break MilkDrop or Fluid.
- Clip ordering is deterministic bytewise order of relative IDs, independent of
  directory enumeration order and locale.

Folder collections contain only their direct video children. Nested folders are
independent collections; this makes “next in this folder” unambiguous. `playlist .`
selects videos directly under the root; `playlist concert/set-a` selects that
folder's direct children. Selecting a clip also selects its parent folder.
There is no reserved video folder called `default`.

## Controls

Use existing `/sideshow/all/<command>` and `/sideshow/<panel-id>/<command>` addresses.
Existing command payloads and pong's `string,string,float` signature remain valid.

| Command | Payload | Video behavior |
|---|---|---|
| `mode` | string `video` | Prepare video and switch when its first frame is ready |
| `preset` | relative clip ID | Load the clip from the beginning and select its parent folder |
| `playlist` | relative directory, or `.` | Load the first usable direct child in sorted order |
| `next`, `prev` | none | Navigate sorted clips in the selected folder, wrapping |
| `random` | none | Choose another usable clip in the selected folder; restart if only one |
| `video_end` | string `loop`, `hold`, `next` | Set end behavior, default `loop` |
| `video_pause` | int 0/1 | Pause/resume the video clock, default unpaused |
| `video_restart` | none | Seek/reopen the current clip at its beginning |
| `lock` | int 0/1 | Suppress automatic clip changes; does not pause playback |
| `master`, `blackout` | existing | Shared final output controls, applied once |
| `duration`, `beat_sensitivity`, `hardcut` | existing | Retain settings for applicable visualizers; do not change video playback |

Unknown end modes and invalid payload types are rejected without changing state.
Video-specific controls outside active or pending video mode log a warning and do
nothing; they do not silently switch modes. `presets.shuffle` does not shuffle video:
ordered navigation and end-next are predictable, and random remains explicit.

Examples:

```bash
python3 tools/osc_send.py mode video
python3 tools/osc_send.py preset 'concert/intro.mp4'
python3 tools/osc_send.py video_end loop
python3 tools/osc_send.py video_end hold
python3 tools/osc_send.py video_end next
python3 tools/osc_send.py video_pause 1
python3 tools/osc_send.py video_pause 0
python3 tools/osc_send.py video_restart
python3 tools/osc_send.py --panel panel-03 playlist 'ambient'
python3 tools/osc_send.py mode fluid
```

## Playback semantics

Loop rewinds at EOF and resumes from the first presented frame. While the decoder
prepares the next iteration, retain the last frame; do not flash black. Frame-perfect
seamless looping is not guaranteed. Hold keeps the last frame indefinitely. Next
loads the next usable direct sibling, wraps at the end, and tries each sibling at
most once per advance attempt. If none loads, retain the last good frame and stop
automatic retries until another explicit command.

While locked, end-next behaves as loop so the chosen visual keeps moving. Explicit
navigation still works. Unlocking restores next behavior at the next EOF. Loop and
hold are unaffected by lock.

Pause freezes the presentation clock and displayed frame. Resume continues from the
same position without a wall-clock catch-up burst. Restart resets the position and
retains the pause setting; a paused restart displays the first frame and remains
paused. Explicit clip/folder selection also retains pause and end settings. Loading
while paused must still prepare and display the first frame.

Changing end behavior while held at EOF applies immediately: loop schedules a
rewind, next schedules advancement (or a rewind while locked), and hold stays held.
When paused at EOF, defer that action until resumed. Repeated hold/end commands must
not repeatedly seek or load. Selecting the current clip explicitly restarts it;
repeating `mode video` while already active leaves playback alone.

Mode changes remember the clip, folder and end setting for the process lifetime.
Leaving video releases its live decoder and GPU resources. Returning starts the
remembered clip at time zero, unpaused, with its remembered end behavior. If that
clip disappeared or fails, try other catalog entries once; if no clip is usable,
retain the current non-video mode. Playback choices are not written back to TOML.

Blackout hides output without pausing playback. All modes continue to share master
smoothing and the existing blackout priority in the bounded command queue.

## Asynchronous loading and timing

The current backend factory initializes synchronously. Extend the controller with
a small explicit pending/ready/failed preparation state so it can retain the active
backend while video opens and decodes. Existing MilkDrop and Fluid preparation stays
synchronous; this is not a general plugin or task framework.

A video mode request does not commit the active mode or status until the first
frame is decoded and its texture is successfully uploaded. Until then the old
backend continues rendering. Sequential `mode video` then `preset ...` commands
route selection and video controls to the pending video request, so operators do
not need timing-dependent sleeps. Shared playback controls update both the active
and pending settings; master/blackout remain owned by main.

A subsequent mode request supersedes pending preparation, including a request for
the already-active mode. A new clip request replaces an older pending clip request.
Use monotonically increasing generation IDs and discard stale frames/errors. Failed
explicit requests keep the current clip/mode and selection. Requests within active
video retain its last picture while preparing the replacement; failed preparation
resumes its existing decoder when possible, otherwise holds that picture with a
warning. Never switch to an unrelated clip for a failed explicit selection.

Use a single application-owned decoder worker with bounded request/result queues,
no detached thread per command, and at most one current plus one candidate decoder
context. Retain at most three completed converted frames in total, plus one frame
being converted and the current GPU image. Worker backpressure uses cancellable
condition-variable waits. Catalog scanning also checks cancellation and does not
run on the rendering thread.

Use presentation timestamps and stream time bases, not a fixed frame count per
render tick. Normalize the first presented frame to time zero; support variable
frame rate and decoder reordering/draining. For absent or unusable timestamps,
advance using valid stream frame rate, then 30 FPS as a last resort. Repair
nonmonotonic output timestamps without moving the presentation clock backward.
Hold the latest due frame, discard obsolete queued frames, and never wait for the
worker in the render loop. If decoding is behind, keep the last picture; bounded
buffering takes priority over trying to catch up in one frame.

Pause and replacement requests wake the worker without requiring it to fill its
frame queue. Set an FFmpeg interrupt callback and a five-second deadline for open,
probe, seek and packet-read operations; cap probing and decoder thread counts.
Cancellation does not join or destroy a worker on the main render path. It cannot
preempt arbitrary CPU decoder code or a blocked filesystem syscall; do not claim a
hard real-time guarantee. Shutdown signals cancellation, wakes queues and joins
the owned thread before destroying application resources.

## Rendering and media limits

Upload converted RGBA8 frames on the main thread and draw an aspect-preserving image
centered on black. Honor sample aspect ratio and common 90-degree rotation metadata.
Do not stretch or crop to fill. Treat alpha as opaque for this separate mode.

Support progressive SDR content in the first release. Convert YUV using declared
range and matrix metadata; use documented BT.709 (HD) or BT.601 (SD) defaults when
missing. Reject HDR transfer functions rather than presenting them as tone-mapped.
Reject interlaced streams with a diagnostic; deinterlacing is outside this release.

Bound source dimensions before allocation: each dimension at most 4096 and at most
8,847,360 source pixels. Downscale converted output to fit a 1920×1080 landscape or
1080×1920 portrait box, preserving aspect ratio; also respect `GL_MAX_TEXTURE_SIZE`.
Validate changing stream dimensions, arithmetic overflow and frame strides. These
are resource ceilings, not a promise that every file within them plays in real time.

Zero-size resize events retain the current targets until a positive drawable returns.
Every video pass establishes required GL state, including sampler bindings, pixel
upload state, framebuffer, viewport, VAO, blend/depth/scissor and color mask. Candidate
initialization failures restore state needed by the retained backend. Shared output
brightness/blackout must remain correct after either successful or failed switches.

## Failure handling and status

Unsupported codec/container, no video stream, invalid path, missing/deleted file,
empty folder, corrupt data, timeout and allocation failure are reported distinctly
in logs. Errors travel through the bounded worker result channel; worker exceptions
must not terminate the process or escape a thread entry point.

A decode failure during playback holds the last good frame and stops that attempt;
explicit select/restart can retry. Automatic next skips failed candidates once per
advance attempt. Do not spin forever on a broken folder or retry a broken stream
every frame. A missing startup video library falls back to MilkDrop. A fatal video
GL rendering failure gets one MilkDrop recovery attempt; failure of that recovery
exits nonzero for systemd. Extend the existing Fluid-only recovery condition to
include Video without changing MilkDrop's preset recovery behavior.

Pong reports `video/<relative-id>` after activation, for example
`video/concert/intro.mp4`; while a candidate is pending it reports the active
backend. Pause, pending-load detail and playback position are not added to pong in
this version. Preserve its existing wire signature.

## Startup configuration and dependencies

```toml
[visuals]
mode = "milkdrop" # milkdrop | fluid | video; existing default unchanged

[video]
root = "/opt/sideshow/videos"
preset = ""      # empty: first usable relative ID in the recursive catalog
end = "loop"     # loop | hold | next
```

Invalid keys use existing per-key warning/default behavior. Explicit startup preset
failure falls back to another usable catalog clip; if none works, fall back to
MilkDrop. Clip fallback attempts are asynchronous and bounded by catalog size.

Require FFmpeg 5.1 or newer, including the Bookworm library API versions, and test
against the development Mac's newer libraries. Update the player dependency checks
and provisioning scripts for the four libraries. Core-only builds stay free of
FFmpeg, SDL, GL and projectM requirements. Tests use generated, redistributable tiny
fixtures rather than depending on user clips or downloading a video library.

Provision `/opt/sideshow/videos` as a directory readable by the service user, without
deleting or replacing existing contents. Document copying videos and nested folders
there. Do not install packages on the developer's machine as part of specification
work. Document the linked FFmpeg build and license implications; do not claim all
FFmpeg builds have identical codec support or licensing.

## Boundaries and acceptance

Keep the recursive catalog and playback state machine GPU/decoder-independent. A
worker module owns FFmpeg and cancellation, a frame presenter owns video textures,
and a video backend composes them behind the visualization controller. Avoid adding
video-specific methods to every renderer; use a narrow optional video-control seam.
Expand mode-indexed storage safely for the third mode and future ISF addition.

Acceptance requires:

1. Core tests: nested/root folders, duplicate basenames, exact relative IDs, path
   rejection, empty/missing/unreadable entries, deterministic navigation, all end
   modes, pause/restart/lock interactions, per-mode memory and pending-request
   supersession. Failed requests preserve active state; controls arriving during
   preparation go to the intended target.
2. Decoder tests with real fixtures: silent handling of files with audio, reordered
   and variable-rate frames, EOF draining, repeated loops, seek/restart, invalid or
   truncated clips, missing timestamps, resource limits, timeout/cancellation and
   bounded queues. Test lifecycle while rapidly replacing requests and shutting down.
3. Real GL tests: aspect fitting, rotation, visible decoded output, blackout,
   pause/resume, repeat mode switches, resize, sampler/pixel-store contamination,
   and candidate allocation failure retaining the old picture and releasing resources.
4. Full macOS build, existing C++/Python regression suites, core sanitizers and
   applicable decoder sanitizer tests. Run actual OSC commands against the player
   and verify `video/<relative-id>` status with nested paths containing spaces.
5. Pi hardware checklist: real target codecs/resolutions, frame pacing, dropped-frame
   behavior, mode switching/blackout latency, missing/corrupt clips, long loops and
   thermal behavior. Mark hardware checks unverified when no Pi is available.

Out of scope: audible video, streaming/network URLs, seeking to arbitrary positions,
playback speed/reverse, seamless transitions or mixing, folder auto-watching, a media
browser/upload UI, hardware acceleration, HDR tone mapping, deinterlacing and
multi-panel synchronization. ISF is subsequent work, not part of this video change.
