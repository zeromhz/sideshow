#!/usr/bin/env python3
"""Generate synthetic H.264 transport streams with mid-stream resolution changes."""
from pathlib import Path
import subprocess
import tempfile


def generate():
    root = Path(__file__).resolve().parents[1] / 'tests/fixtures/video'
    with tempfile.TemporaryDirectory() as tmp:
        def segment(name, size, frames, offset=0):
            path = Path(tmp) / name
            subprocess.run([
                'ffmpeg', '-hide_banner', '-loglevel', 'error', '-y',
                '-f', 'lavfi', '-i', f'color=black:size={size}:rate=1',
                '-frames:v', str(frames), '-c:v', 'libx264', '-preset', 'ultrafast',
                '-tune', 'zerolatency', '-output_ts_offset', str(offset),
                '-f', 'mpegts', str(path)], check=True)
            return path.read_bytes()
        small = segment('small.ts', '64x64', 10)
        for name, size in [('size-change', '4096x2304'), ('wide-change', '4112x32'),
                           ('tall-change', '32x4112'), ('valid-change', '128x64')]:
            large = segment('large.ts', size, 1, 10)
            (root / f'{name}.ts').write_bytes(small + large)
        (root / 'oversized.ts').write_bytes(segment('oversized.ts', '4096x2304', 1))


if __name__ == '__main__':
    generate()
