#!/usr/bin/env python3
"""Send sideshow OSC commands to one panel or the whole stage (stdlib only).

Examples:
  tools/osc_send.py next                          # every panel (broadcast)
  tools/osc_send.py --panel panel-03 blackout 1   # one panel by id
  tools/osc_send.py --host 127.0.0.1 master 0.5   # a specific host
  tools/osc_send.py ping                          # list panels that answer
"""
import argparse
import math
import socket
import struct
import sys
import time

ARG_TYPES = {
    "next": None,
    "prev": None,
    "random": None,
    "lock": int,
    "blackout": int,
    "hardcut": int,
    "master": float,
    "duration": float,
    "beat_sensitivity": float,
    "playlist": str,
    "preset": str,
}

FLOAT32_MAX = 3.4028234663852886e38


def _pad(data: bytes) -> bytes:
    """Null-terminate and pad to a multiple of 4 bytes (OSC string rule)."""
    return data + b"\0" * (4 - len(data) % 4)


def encode_message(address, args):
    tags = ","
    payload = b""
    for arg in args:
        if isinstance(arg, bool) or not isinstance(arg, (int, float, str)):
            raise TypeError(f"unsupported OSC argument: {arg!r}")
        if isinstance(arg, int):
            tags += "i"
            payload += struct.pack(">i", arg)
        elif isinstance(arg, float):
            tags += "f"
            payload += struct.pack(">f", arg)
        else:
            tags += "s"
            payload += _pad(arg.encode())
    return _pad(address.encode()) + _pad(tags.encode()) + payload


def parse_value(command, text):
    """Return the OSC argument list for `command`, or raise ValueError with a readable message.

    Toggles (int) accept any number and send 1 for non-zero, 0 for zero, like the player.
    Floats must be finite and fit in a float32.
    """
    arg_type = ARG_TYPES[command]
    if arg_type is None:
        if text is not None:
            raise ValueError(f"{command} takes no value")
        return []
    if text is None:
        raise ValueError(f"{command} needs a value")
    if arg_type is str:
        return [text]
    try:
        number = float(text)
    except ValueError:
        raise ValueError(f"{command} needs a number, got {text!r}") from None
    if not math.isfinite(number) or abs(number) > FLOAT32_MAX:
        raise ValueError(f"{command} value out of range: {text!r}")
    if arg_type is int:
        return [1 if number != 0 else 0]
    return [number]


def _read_string(data, offset):
    end = data.index(b"\0", offset)
    text = data[offset:end].decode(errors="replace")
    return text, offset + ((end - offset) // 4 + 1) * 4


def decode_message(data):
    address, offset = _read_string(data, 0)
    tags, offset = _read_string(data, offset)
    args = []
    for tag in tags[1:]:
        if tag == "i":
            args.append(struct.unpack_from(">i", data, offset)[0])
            offset += 4
        elif tag == "f":
            args.append(struct.unpack_from(">f", data, offset)[0])
            offset += 4
        elif tag == "s":
            text, offset = _read_string(data, offset)
            args.append(text)
        else:
            raise ValueError(f"unsupported OSC type tag {tag!r}")
    return address, args


def ping(sock, host, port, wait_seconds=1.0):
    sock.sendto(encode_message("/sideshow/ping", []), (host, port))
    sock.settimeout(0.2)
    deadline = time.monotonic() + wait_seconds
    while time.monotonic() < deadline:
        try:
            data, addr = sock.recvfrom(4096)
        except socket.timeout:
            continue
        address, args = decode_message(data)
        if address == "/sideshow/pong" and len(args) == 3:
            panel, preset, fps = args
            print(f"{addr[0]:15}  {panel:12}  {fps:5.1f} fps  {preset}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="255.255.255.255", help="panel IP or broadcast address")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--panel", default="all", help="panel id, or 'all' (default)")
    parser.add_argument("command", choices=sorted(ARG_TYPES) + ["ping"])
    parser.add_argument("value", nargs="?")
    options = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    if options.command == "ping":
        ping(sock, options.host, options.port)
        return 0

    try:
        args = parse_value(options.command, options.value)
    except ValueError as error:
        parser.error(str(error))
    address = f"/sideshow/{options.panel}/{options.command}"
    sock.sendto(encode_message(address, args), (options.host, options.port))
    return 0


if __name__ == "__main__":
    sys.exit(main())
