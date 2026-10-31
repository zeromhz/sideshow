# HEVC fixture

Synthetic, generated locally (requires FFmpeg with libx265; see tools/generate_video_fixtures.py):

```sh
ffmpeg -f lavfi -i testsrc2=size=64x48:rate=5 -t 0.6 -c:v libx265 -x265-params log-level=error -tag:v hvc1 -pix_fmt yuv420p hevc.mp4
```

Three 64x48 frames at 0, 0.2 and 0.4 s. Kept out of ../video so the clip order there is unchanged.
