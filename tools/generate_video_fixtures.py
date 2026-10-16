#!/usr/bin/env python3
"""Generate tiny synthetic test media; requires FFmpeg with libx264."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1] / 'tests/fixtures/video'
root.mkdir(parents=True, exist_ok=True)
version = subprocess.check_output(['ffmpeg', '-version'], text=True).splitlines()[0]
commands = []
def generate(name, *args):
    cmd = ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', *args, str(root/name)]
    subprocess.run(cmd, check=True)
    commands.append(' '.join(cmd))
source = ['-f', 'lavfi', '-i', 'testsrc2=size=64x48:rate=5']
generate('bars-audio.avi', *source, '-f','lavfi','-i','sine=frequency=440','-t','0.6','-c:v','mpeg4','-bf','2','-c:a','pcm_s16le')
generate('reordered.mp4', *source, '-t','0.6','-c:v','libx264','-bf','2','-pix_fmt','yuv420p')
generate('variable.mkv', *source, '-frames:v','3','-vf',"setpts='if(eq(N,2),0.8/TB,N*0.2/TB)'",'-fps_mode','vfr','-c:v','ffv1')
generate('aspect.mp4', *source, '-t','0.6','-vf','setsar=2/1','-c:v','libx264')
generate('rotated.mp4','-display_rotation','90','-i',str(root/'aspect.mp4'),'-c','copy')
generate('audio-only.mov','-f','lavfi','-i','sine=frequency=440','-t','0.1','-c:a','pcm_s16le')
generate('interlaced.mkv', *source, '-t','0.6','-vf','setfield=tff','-flags','+ilme+ildct','-c:v','mpeg2video')
generate('hdr.mkv', *source, '-t','0.6','-vf','setparams=color_trc=smpte2084','-c:v','ffv1')
(root/'corrupt.mp4').write_bytes(b'not a video')
data=(root/'reordered.mp4').read_bytes()
(root/'truncated.mp4').write_bytes(data[:len(data)//2])
(root/'README.md').write_text('# Synthetic video fixtures\n\nGenerated locally; no third-party footage.\n\n'+version+'\n\n```sh\n'+'\n'.join(commands)+'\n```\n\nCorrupt is plain text; truncated is the first half of reordered.mp4.\n')
