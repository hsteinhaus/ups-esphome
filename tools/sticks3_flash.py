#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Flash a StickS3 that is still running UiFlow2, without a button press.

esptool cannot do it: UiFlow2 presents a TinyUSB CDC port rather than the ROM's
USB-Serial-JTAG, so DTR/RTS carry no hardware reset and merely wedge the
firmware -- the port vanishes and only the side button brings it back. The board
also has its own cell, so neither the host nor a hub can power-cycle it.

Side button, per M5Stack's documentation: single click powers on or resets,
double click powers off, long press enters download mode with the green LED
flashing. A board already in download mode is flashed here without any reset.

Every step here therefore fails safe: it does nothing it cannot first confirm,
and leaves the board enumerated rather than risking the state that costs a
button press. Pass --force to skip the REPL checks.

Usage: tools/sticks3_flash.py <config.yaml> [--timeout S] [--force]
"""
import argparse
import glob
import subprocess
import sys
import time

import serial

BY_ID = "/dev/serial/by-id/"
APP_MATCH = "StickS3"   # UiFlow2's own CDC
# The ROM usually comes up as "Espressif USB JTAG serial debug unit", but the
# product string is not a contract: match on "not the app port" instead, so a
# board sitting in download mode is never missed over its name. esptool detects
# the chip itself.
ROM_WAIT = 90

PROMPT = b">>>"


def ports(match):
    return sorted(p for p in glob.glob(BY_ID + "*") if match in p)


def rom_ports():
    return sorted(p for p in glob.glob(BY_ID + "*") if APP_MATCH not in p)


def wait_for(match, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if found := ports(match):
            return found[0]
        time.sleep(0.5)
    return None


def wait_for_rom(timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if found := rom_ports():
            # Let the kernel finish binding before esptool opens it.
            time.sleep(1.0)
            return found[0]
        time.sleep(0.5)
    return None


def open_quietly(port):
    """Open without asserting DTR/RTS.

    pyserial raises both on open, and on this CDC stack that is itself enough to
    reset the board -- the very outcome this tool exists to avoid.
    """
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 1
    ser.dtr = False
    ser.rts = False
    ser.open()
    return ser


def ask(ser, line, settle=0.4):
    ser.reset_input_buffer()
    ser.write(line + b"\r\n")
    ser.flush()
    time.sleep(settle)
    return ser.read(ser.in_waiting or 1)


def enter_download_mode(port, force):
    with open_quietly(port) as ser:
        # Ctrl-C only interrupts a running script; it cannot reset the board.
        ser.write(b"\x03\x03")
        ser.flush()
        time.sleep(0.4)
        banner = ask(ser, b"")

        if not force:
            if PROMPT not in banner:
                return "no MicroPython prompt -- left the board alone"
            if b"True" not in ask(ser, b'import machine; print(hasattr(machine, "bootloader"))'):
                return "machine.bootloader() is unavailable -- left the board alone"

        # Confirmed reachable: this is the one command that drops the port.
        ser.write(b"machine.bootloader()\r\n")
        ser.flush()
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("config")
    parser.add_argument("--timeout", type=float, default=7200.0)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    if rom := (rom_ports() or [None])[0]:
        print(f"already in ROM download mode at {rom}", flush=True)
    else:
        print(f"waiting up to {args.timeout:.0f}s for the board ...", flush=True)
        app = wait_for(APP_MATCH, args.timeout)
        if app is None:
            sys.exit(
                "no StickS3 port appeared; board left untouched. A double click powers "
                "it off, a single click powers it on, a long press enters download mode"
            )
        print(f"found {app}", flush=True)

        if problem := enter_download_mode(app, args.force):
            sys.exit(f"{problem} (still on the bus; --force overrides)")

        rom = wait_for_rom(ROM_WAIT)
        if rom is None:
            sys.exit(
                f"no port appeared within {ROM_WAIT}s of machine.bootloader(). "
                "long-press the side button until the green LED flashes"
            )
        print(f"ROM bootloader at {rom}", flush=True)

    cmd = ["work/venv/bin/esphome", "upload", args.config, "--device", rom]
    print(" ".join(cmd), flush=True)
    sys.exit(subprocess.call(cmd))


if __name__ == "__main__":
    main()
