#!/usr/bin/env python3
"""Flash a StickS3 that is still running UiFlow2, without touching the board.

esptool's DTR/RTS auto-reset does nothing useful here: UiFlow2 presents a
TinyUSB CDC port, not the ROM's USB-Serial-JTAG, so the toggle has no hardware
meaning and merely wedges the firmware -- the port disappears and nothing comes
back. MicroPython can be asked directly instead: machine.bootloader() enters ROM
download mode on purpose, and the ROM then enumerates under a different by-id
name, which this follows.

Usage: tools/sticks3_flash.py <config.yaml> [timeout_s]
"""
import glob
import subprocess
import sys
import time

import serial

BY_ID = "/dev/serial/by-id/"
APP_MATCH = "StickS3"       # UiFlow2's own CDC
ROM_MATCH = "USB_JTAG"      # what the ROM bootloader enumerates as


def ports(match):
    return sorted(p for p in glob.glob(BY_ID + "*") if match in p)


def wait_for(match, timeout, invert=False):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        found = ports(match)
        if bool(found) != invert:
            return found[0] if found else True
        time.sleep(0.5)
    return None


def enter_download_mode(port):
    # Ctrl-C twice to break whatever UiFlow2 is running, then ask for the ROM.
    with serial.Serial(port, 115200, timeout=1) as ser:
        ser.write(b"\x03\x03\r\n")
        time.sleep(0.3)
        ser.reset_input_buffer()
        ser.write(b"import machine; machine.bootloader()\r\n")
        ser.flush()


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: sticks3_flash.py <config.yaml> [timeout_s]")
    config, timeout = sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 600.0

    print(f"waiting up to {timeout:.0f}s for the board to appear ...", flush=True)
    app = wait_for(APP_MATCH, timeout)
    if app is None:
        sys.exit("no StickS3 CDC port appeared -- reset the board (hold ~6s, then press)")
    print(f"found {app}", flush=True)

    # Already in the ROM? Then skip straight to flashing.
    if not ports(ROM_MATCH):
        print("asking MicroPython for ROM download mode ...", flush=True)
        try:
            enter_download_mode(app)
        except serial.SerialException as err:
            print(f"REPL write failed ({err}); the port may already be resetting", flush=True)

        rom = wait_for(ROM_MATCH, 30)
        if rom is None:
            sys.exit("the ROM bootloader never enumerated -- hold button A while powering on")
    rom = ports(ROM_MATCH)[0]
    print(f"ROM bootloader at {rom}", flush=True)

    cmd = ["work/venv/bin/esphome", "upload", config, "--device", rom]
    print(" ".join(cmd), flush=True)
    sys.exit(subprocess.call(cmd))


if __name__ == "__main__":
    main()
