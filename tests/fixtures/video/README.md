# Synthetic video fixtures

Generated locally; no third-party footage.

ffmpeg version 9.0.2 Copyright (c) 2000-2026 the FFmpeg developers

```sh
ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc2=size=64x48:rate=5 -f lavfi -i sine=frequency=440 -t 0.6 -c:v mpeg4 -bf 2 -c:a pcm_s16le /Users/joshm/.codex/worktrees/selectable-video-mode/sideshow/tests/fixtures/video/bars-audio.avi
ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc2=size=64x48:rate=5 -t 0.6 -c:v libx264 -bf 2 -pix_fmt yuv420p /Users/joshm/.codex/worktrees/selectable-video-mode/sideshow/tests/fixtures/video/reordered.mp4
ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc2=size=64x48:rate=5 -frames:v 3 -vf setpts='if(eq(N,2),0.8/TB,N*0.2/TB)' -fps_mode vfr -c:v ffv1 /Users/joshm/.codex/worktrees/selectable-video-mode/sideshow/tests/fixtures/video/variable.mkv
ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc2=size=64x48:rate=5 -t 0.6 -vf setsar=2/1 -c:v libx264 /Users/joshm/.codex/worktrees/selectable-video-mode/sideshow/tests/fixtures/video/aspect.mp4
ffmpeg -hide_banner -loglevel error -y -display_rotation 90 -i /Users/joshm/.codex/worktrees/selectable-video-mode/sideshow/tests/fixtures/video/aspect.mp4 -c copy /Users/joshm/.codex/worktrees/selectable-video-mode/sideshow/tests/fixtures/video/rotated.mp4
ffmpeg -hide_banner -loglevel error -y -f lavfi -i sine=frequency=440 -t 0.1 -c:a pcm_s16le /Users/joshm/.codex/worktrees/selectable-video-mode/sideshow/tests/fixtures/video/audio-only.mov
ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc2=size=64x48:rate=5 -t 0.6 -vf setfield=tff -flags +ilme+ildct -c:v mpeg2video /Users/joshm/.codex/worktrees/selectable-video-mode/sideshow/tests/fixtures/video/interlaced.mkv
ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc2=size=64x48:rate=5 -t 0.6 -vf setparams=color_trc=smpte2084 -c:v ffv1 /Users/joshm/.codex/worktrees/selectable-video-mode/sideshow/tests/fixtures/video/hdr.mkv
```

Corrupt is plain text; truncated is the first half of reordered.mp4.

## Decoder allocation limits

Run `python3 tools/generate_video_limit_fixtures.py` to regenerate the synthetic
H.264 transport streams. Each `*-change.ts` contains ten black 64×64 frames followed
by one frame at the new size: 4096×2304 (pixel ceiling), 4112×32 or 32×4112 (axis
ceiling), or 128×64 (valid change). `oversized.ts` starts at 4096×2304 to exercise
initial probing. No third-party footage is used. These test decoder content
handling directly; `.ts` is not added to the selectable library extensions.
