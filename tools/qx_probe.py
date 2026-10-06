#!/usr/bin/env python3
"""Identify the protocol dialect of a Cypress-bridged (Megatec/Q*) UPS.

Run this on the Linux host the UPS is plugged into, before writing any
firmware for it: `0665:5161` is a generic USB-to-serial bridge shared by
several UPS families that answer different commands, and the only reliable
way to tell them apart is to ask.

    sudo ./tools/qx_probe.py

The transport is the one NUT calls `cypress`: commands go out as 8-byte HID
output reports, the reply arrives on the interrupt IN endpoint in 8-byte
chunks and ends with a carriage return. hidraw gives us both.

Only status queries are ever sent. Commands that start a battery test or a
shutdown are refused outright -- this is a live UPS, and a `S01R0002` typed
by accident cuts the load.
"""

from __future__ import annotations

import argparse
import os
import select
import sys
import time

CYPRESS_VID, CYPRESS_PID = 0x0665, 0x5161

CHUNK = 8
READ_TIMEOUT = 2.0

# Every probe below only reads. The refusal list is not advisory: a `T` runs
# the battery down on purpose and an `S` drops the load.
PROBES = [
    ("Q1", "Megatec status inquiry -- the one that matters"),
    ("QS", "Status inquiry used by units that do not answer Q1"),
    ("F", "Nameplate ratings"),
    ("I", "Vendor, model and firmware"),
    ("QMOD", "Voltronic P31 mode"),
    ("QGS", "Voltronic P31 general status"),
]
FORBIDDEN = ("S", "T", "C")


def forbidden(cmd: str) -> bool:
    """True for anything that switches the load, tests the battery or beeps."""
    if cmd in ("Q1", "QS", "QMOD", "QGS", "QRI", "QBV"):
        return False
    return cmd.startswith(FORBIDDEN) or cmd == "Q"


def find_hidraw(vid: int, pid: int) -> list[str]:
    """Return every hidraw node belonging to the given USB id."""
    want = f"{vid:08X}:{pid:08X}".lower()
    found = []
    base = "/sys/class/hidraw"
    for name in sorted(os.listdir(base)):
        uevent = os.path.join(base, name, "device", "uevent")
        try:
            with open(uevent) as fh:
                text = fh.read().lower()
        except OSError:
            continue
        for line in text.splitlines():
            if line.startswith("hid_id=") and line.endswith(want):
                found.append(f"/dev/{name}")
    return found


def exchange(fd: int, cmd: str, timeout: float = READ_TIMEOUT) -> bytes:
    """Send one command and collect the reply up to its carriage return."""
    payload = (cmd + "\r").encode("ascii")

    # Drain anything the previous command left behind, or its tail is read as
    # this command's answer and every reply after the first looks corrupt.
    while select.select([fd], [], [], 0)[0]:
        try:
            os.read(fd, 64)
        except OSError:
            break

    for off in range(0, len(payload), CHUNK):
        chunk = payload[off : off + CHUNK].ljust(CHUNK, b"\x00")
        os.write(fd, b"\x00" + chunk)

    reply = bytearray()
    deadline = time.monotonic() + timeout
    while b"\r" not in reply and time.monotonic() < deadline:
        if not select.select([fd], [], [], deadline - time.monotonic())[0]:
            break
        try:
            reply += os.read(fd, 64)
        except OSError as exc:
            return bytes(reply) + f"<read error: {exc}>".encode()
    return bytes(reply)


def describe_q1(reply: str) -> list[str]:
    """Decode a Megatec Q1 reply into labelled fields."""
    body = reply.strip().lstrip("(")
    parts = body.split()
    names = [
        ("input voltage", "V"),
        ("input fault voltage", "V"),
        ("output voltage", "V"),
        ("load", "%"),
        ("input frequency", "Hz"),
        ("battery voltage", "V"),
        ("temperature", "C"),
    ]
    out = []
    for (label, unit), value in zip(names, parts):
        out.append(f"    {label:22s} {value} {unit}")
    if len(parts) > len(names):
        bits = parts[len(names)]
        flags = [
            "utility fail",
            "battery low",
            "boost/buck active",
            "UPS failed",
            "standby (not online)",
            "test in progress",
            "shutdown active",
            "beeper on",
        ]
        out.append(f"    status bits            {bits}")
        for flag, bit in zip(flags, bits):
            out.append(f"      {flag:24s} {bit}")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--device", help="hidraw node, when autodetection finds several")
    ap.add_argument("--vid", type=lambda s: int(s, 16), default=CYPRESS_VID)
    ap.add_argument("--pid", type=lambda s: int(s, 16), default=CYPRESS_PID)
    ap.add_argument("--extra", action="append", default=[],
                    help="additional query to try (shutdown and test commands are refused)")
    ap.add_argument("--repeat", type=int, default=1,
                    help="repeat the Q1 probe N times, to see which fields move")
    args = ap.parse_args()

    node = args.device
    if node is None:
        candidates = find_hidraw(args.vid, args.pid)
        if not candidates:
            print(f"no hidraw node for {args.vid:04x}:{args.pid:04x}", file=sys.stderr)
            return 1
        if len(candidates) > 1:
            print(f"several candidates, pick one with --device: {candidates}", file=sys.stderr)
            return 1
        node = candidates[0]

    for cmd in args.extra:
        if forbidden(cmd):
            print(f"refusing {cmd!r}: it tests the battery or switches the load", file=sys.stderr)
            return 1

    print(f"probing {node} ({args.vid:04x}:{args.pid:04x})\n")
    try:
        fd = os.open(node, os.O_RDWR)
    except PermissionError:
        print(f"{node} needs root, or a udev rule granting access", file=sys.stderr)
        return 1

    try:
        probes = list(PROBES) + [(c, "requested") for c in args.extra]
        for cmd, why in probes:
            reply = exchange(fd, cmd)
            text = reply.decode("ascii", "replace").strip()
            print(f"{cmd:6s} {why}")
            if not reply:
                print("    (no answer)\n")
                continue
            print(f"    raw   {reply!r}")
            print(f"    ascii {text}")
            if cmd in ("Q1", "QS") and text.startswith("("):
                print("\n".join(describe_q1(text)))
            print()

        for n in range(1, args.repeat):
            time.sleep(1.0)
            text = exchange(fd, "Q1").decode("ascii", "replace").strip()
            print(f"Q1 #{n + 1}  {text}")
    finally:
        os.close(fd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
