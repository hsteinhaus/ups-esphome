#!/usr/bin/env python3
"""Flash a StickS3 and prove the result, unattended.

Flashing is only half of it: a board that takes the image and then fails to
bring up its LCD rail or its USB host looks identical, from the flasher's point
of view, to one that worked. So this drives the whole path -- download mode,
flash, join, verify -- and ends with a verdict rather than an exit code.

Safety: nothing is reset until a port is actually present and, for the UiFlow2
CDC, until its REPL has answered. A board already in download mode is flashed
with no reset at all. The port is opened with DTR/RTS low, which on this CDC
stack is itself enough to reset the board.

Side button, per M5Stack: single click powers on or resets, double click powers
off, long press enters download mode with the green LED flashing.

Usage: tools/sticks3_deploy.py <config.yaml> [--timeout S] [--subnet 10.22.10]
                               [--no-reset] [--skip-flash]
"""
import argparse
import glob
import json
import re
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor

import serial

BY_ID = "/dev/serial/by-id/"
APP_MATCH = "StickS3"
PROMPT = b">>>"
ROM_WAIT = 120
ESPHOME = "work/venv/bin/esphome"
# Keyed by entity NAME ("Status"), not by the YAML id -- the two differ, and
# using the id matches nothing, which reads exactly like an absent device.
STATUS_ENTITY = "binary_sensor/status"


def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


# ---------------------------------------------------------------- flashing

def ports(match=""):
    return sorted(p for p in glob.glob(BY_ID + "*") if match in p)


def rom_ports():
    return [p for p in ports() if APP_MATCH not in p]


def wait_until(fn, timeout, settle=0.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if found := fn():
            time.sleep(settle)
            return found[0] if isinstance(found, list) else found
        time.sleep(0.5)
    return None


def open_quietly(port):
    """Open without raising DTR/RTS, which would reset the board on open."""
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, 115200, 1
    ser.dtr = ser.rts = False
    ser.open()
    return ser


def enter_download_mode(port):
    with open_quietly(port) as ser:
        ser.write(b"\x03\x03")  # interrupt a running script; cannot reset
        ser.flush()
        time.sleep(0.4)
        ser.reset_input_buffer()
        ser.write(b"\r\n")
        time.sleep(0.4)
        if PROMPT not in ser.read(ser.in_waiting or 1):
            return "no MicroPython prompt"
        ser.write(b'import machine; print(hasattr(machine, "bootloader"))\r\n')
        time.sleep(0.4)
        if b"True" not in ser.read(ser.in_waiting or 1):
            return "machine.bootloader() unavailable"
        log("REPL confirmed; requesting ROM download mode")
        ser.write(b"machine.bootloader()\r\n")
        ser.flush()
    return None


def flash(config, timeout, allow_reset):
    if rom := (rom_ports() or [None])[0]:
        log(f"already in download mode at {rom}")
    else:
        log(f"waiting up to {timeout:.0f}s for a port")
        app = wait_until(lambda: ports(APP_MATCH), timeout)
        if app is None:
            return "no port appeared (single click powers it on, long press enters download mode)"
        log(f"found {app}")
        if not allow_reset:
            return "only the UiFlow2 port is present and resets are disabled"
        if problem := enter_download_mode(app):
            return f"{problem}; board left on the bus"
        rom = wait_until(rom_ports, ROM_WAIT, settle=1.0)
        if rom is None:
            return f"no port within {ROM_WAIT}s of machine.bootloader(); long-press for download mode"
        log(f"download mode at {rom}")

    log("flashing")
    if subprocess.call([ESPHOME, "upload", config, "--device", rom]) != 0:
        return "esphome upload failed"

    # machine.bootloader() sets a force-download flag that an ordinary reset
    # does not clear, so the board comes straight back to "waiting for
    # download" and never runs what was just written. A watchdog reset is the
    # documented way out on USB-Serial-JTAG parts.
    log("watchdog reset to leave download mode")
    if subprocess.call(["work/venv/bin/esptool", "--port", rom,
                        "--after", "watchdog-reset", "run"]) != 0:
        return "could not reset out of download mode"
    return None


# ------------------------------------------------------------ verification

def uses_m5pm1(config):
    """True when this build includes the PMIC, packages included."""
    from pathlib import Path

    root = Path(config).resolve().parent
    text = Path(config).read_text()
    for include in re.findall(r"!include\s+(\S+)", text):
        path = root / include
        if path.exists():
            text += path.read_text()
    return "m5pm1" in text


def device_name(config):
    text = open(config).read()
    match = re.search(r"^\s*name:\s*([\w-]+)\s*$", text, re.M)
    return match.group(1) if match else None


def probe(host, path, timeout=2.0):
    try:
        with urllib.request.urlopen(f"http://{host}{path}", timeout=timeout) as resp:
            return json.loads(resp.read().decode())
    except (urllib.error.URLError, OSError, ValueError, socket.timeout):
        return None


def http_open(host):
    try:
        with socket.create_connection((host, 80), timeout=1):
            return host
    except OSError:
        return None


def find_device(subnet, name, timeout, identify=None):
    """mDNS first, then a sweep -- a new MAC usually means a new lease.

    Returns every ESPHome host found. Picking the first match once verified a
    different board entirely and reported its missing component as a failure,
    so the caller decides when there is more than one.
    """
    deadline = time.monotonic() + timeout
    probe_path = f"/{identify}" if identify else f"/{STATUS_ENTITY}"
    while time.monotonic() < deadline:
        try:
            if host := socket.gethostbyname(f"{name}.local"):
                if probe(host, probe_path) is not None:
                    return [host]
        except OSError:
            pass
        with ThreadPoolExecutor(max_workers=64) as pool:
            live = [h for h in pool.map(http_open, (f"{subnet}.{i}" for i in range(1, 255))) if h]
        # The web API keys entities by NAME, not by the YAML id.
        if found := [h for h in live if probe(h, probe_path) is not None]:
            return found
        time.sleep(3)
    return []


def capture_log(config, host, seconds=35):
    try:
        out = subprocess.run(
            [ESPHOME, "logs", config, "--device", host],
            capture_output=True, text=True, timeout=seconds,
        )
        return out.stdout + out.stderr
    except subprocess.TimeoutExpired as expired:
        return (expired.stdout or b"").decode(errors="replace") if isinstance(expired.stdout, bytes) \
            else (expired.stdout or "")


def verify(config, host):
    """Each check is something that silently breaks on a new board."""
    results = []

    entities = {
        "status binary sensor": f"/{STATUS_ENTITY}",
        "backlight (display stack built)": "/light/backlight",
        "UPS load sensor (apc_ups wired)": "/sensor/ups_load",
    }
    for label, path in entities.items():
        results.append((label, probe(host, path) is not None))

    text = capture_log(config, host)
    # Absence of an error proves nothing when the component is absent too, so
    # the PMIC is only checked on boards that have one, and then by its own
    # dump_config banner. The LCD rail is what fails invisibly here: a dark
    # panel, no error anywhere.
    if uses_m5pm1(config):
        results.append(("M5PM1 PMIC reported itself", "M5PM1 PMIC" in text))
        results.append(("PMIC answered on I2C", "PMIC did not answer" not in text))
        results.append(("LCD rail enabled", "could not enable the LCD rail" not in text))
    results.append(("no component marked failed", "mark_failed" not in text and "] Failed" not in text))
    results.append(("USB host started", "usb_host" in text.lower() or "USBClient" in text))
    return results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("config")
    parser.add_argument("--timeout", type=float, default=7200.0)
    parser.add_argument("--subnet", default="10.22.10")
    parser.add_argument("--no-reset", action="store_true")
    parser.add_argument("--skip-flash", action="store_true")
    parser.add_argument("--host", help="skip discovery and verify this address")
    parser.add_argument("--identify", help="entity path unique to this board, e.g. binary_sensor/button_a")
    args = parser.parse_args()

    if not args.skip_flash:
        if problem := flash(args.config, args.timeout, not args.no_reset):
            sys.exit(f"FLASH FAILED: {problem}")
        log("flash complete; waiting for the board to join")

    name = device_name(args.config)
    if args.host:
        host = args.host
    else:
        hosts = find_device(args.subnet, name, 180, args.identify)
        if not hosts:
            sys.exit(f"VERIFY FAILED: {name} never appeared on {args.subnet}.0/24")
        if len(hosts) > 1:
            sys.exit(
                f"VERIFY FAILED: {len(hosts)} ESPHome hosts match ({', '.join(hosts)}). "
                "Pass --identify <entity unique to this board> or --host"
            )
        host = hosts[0]
    log(f"device at {host}")

    results = verify(args.config, host)
    width = max(len(label) for label, _ in results)
    for label, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'}  {label:<{width}}", flush=True)

    failed = [label for label, ok in results if not ok]
    print(f"\n{host}  {len(results) - len(failed)}/{len(results)} checks passed", flush=True)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
