#!/usr/bin/env python3
"""Opt-in real-display OSC smoke test. Importing this module starts nothing."""
import argparse
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time

from osc_send import decode_message, encode_message


def run_live(player):
    repo = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="sideshow-isf-live-") as directory:
        root = Path(directory)
        shutil.copytree(repo / "assets/isf", root / "isf")
        (root / "isf/broken.fs").write_text('/*{"ISFVSN":"2.0"}*/\nvoid main(){invalid glsl;}')
        (root / "videos").mkdir()
        shutil.copy(repo / "tests/fixtures/video/reordered.mp4", root / "videos/clip.mp4")
        (root / "presets").mkdir()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        config = root / "config.toml"
        config.write_text(f'''panel_id = "isf-live"
[display]
width = 1280
height = 720
fullscreen = false
[audio]
device = "sideshow-nonexistent-test-microphone"
[osc]
port = {port}
[presets]
root = {json.dumps(str(root / 'presets'))}
textures = {json.dumps(str(root / 'presets'))}
duration = 3600.0
[visuals]
mode = "isf"
[isf]
root = {json.dumps(str(root / 'isf'))}
preset = "examples/plasma.fs"
max_width = 1280
max_height = 720
[video]
root = {json.dumps(str(root / 'videos'))}
preset = "clip.mp4"
''')
        with (root / "player.log").open("w+") as log, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(("127.0.0.1", 0))
            sock.settimeout(0.3)
            process = subprocess.Popen([str(player), "--config", str(config)], cwd=repo, stdout=log, stderr=log)
            try:
                def send(command, *values):
                    subprocess.run([sys.executable, str(repo / "tools/osc_send.py"), "--host", "127.0.0.1", "--port", str(port), command, *map(str, values)], check=True)

                def status(expected, timeout=12):
                    deadline = time.monotonic() + timeout
                    latest = None
                    # Pong publishes once per second: allow previous status to expire.
                    time.sleep(1.2)
                    while time.monotonic() < deadline:
                        if process.poll() is not None:
                            raise RuntimeError(f"Player exited {process.returncode}; a real display/GL context is required")
                        sock.sendto(encode_message("/sideshow/ping", []), ("127.0.0.1", port))
                        try:
                            address, latest = decode_message(sock.recv(4096))
                            if address == "/sideshow/pong" and len(latest) == 3 and expected(latest[1]):
                                return latest
                        except socket.timeout:
                            pass
                        time.sleep(0.1)
                    raise AssertionError(f"Expected selection not published; last pong: {latest}")

                def selected(name):
                    return status(lambda value: value == name)

                def rss():
                    return int(subprocess.check_output(["ps", "-o", "rss=", "-p", str(process.pid)], text=True).strip())

                print("Startup:", selected("isf/examples/plasma.fs"), flush=True)
                send("lock", 1)
                send("isf_param", "speed", 1.5)
                send("isf_param", "tint", 0.1, 0.5, 1.0, 1.0)
                selected("isf/examples/plasma.fs")
                send("preset", "examples/feedback.fs")
                send("isf_event", "reset")
                selected("isf/examples/feedback.fs")
                for args in (["decay", "wrong"], ["decay", float("nan")], ["decay", 1, 2]):
                    sock.sendto(encode_message("/sideshow/all/isf_param", args), ("127.0.0.1", port))
                selected("isf/examples/feedback.fs")
                send("preset", "broken.fs")
                selected("isf/examples/feedback.fs")
                send("blackout", 1)
                selected("isf/examples/feedback.fs")
                send("blackout", 0)
                samples = []
                for cycle in range(4):
                    for mode, match in (("fluid", lambda s: s.startswith("fluid/")), ("video", lambda s: s == "video/clip.mp4"), ("milkdrop", lambda s: not s.startswith(("fluid/", "video/", "isf/")))):
                        print("Switch cycle", cycle, mode, flush=True)
                        send("mode", mode)
                        status(match)
                        send("mode", "isf")
                        selected("isf/examples/feedback.fs")
                    send("preset", "examples/plasma.fs")
                    result = selected("isf/examples/plasma.fs")
                    samples.append({"cycle": cycle, "rss_kib": rss(), "fps": result[2]})
                    send("preset", "examples/feedback.fs")
                    selected("isf/examples/feedback.fs")
                print("Live OSC checks passed; desktop samples:", json.dumps(samples), flush=True)
                print("Pong verifies selection/liveness; GL tests verify parameters, events and output pixels.")
            except BaseException:
                log.flush()
                log.seek(0)
                print(log.read()[-16000:], file=sys.stderr)
                raise
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-live", action="store_true", help="launch a windowed player and exercise local OSC")
    parser.add_argument("--player", type=Path, default=Path(__file__).resolve().parents[1] / "build/sideshow-player")
    options = parser.parse_args()
    if not options.run_live:
        parser.error("explicit --run-live required (real display and built player needed)")
    run_live(options.player.resolve())


if __name__ == "__main__":
    main()
