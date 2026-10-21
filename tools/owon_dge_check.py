#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Hardware check for the DGE1060 entities: set each one over the web API and
wait for the generator's read-back to arrive as a state event.

Every entity shows what the generator reports, so a set that lands proves the
whole path: entity -> SCPI set -> read-back -> publish. Changes the generator's
settings and toggles its output, and restores the settings it found at the end.
Run only with nothing connected to the output.

    tools/owon_dge_check.py HOST

STICK_IFACE binds to one network interface, for a host whose default route
does not reach the stick.
"""
import json
import math
import os
import socket
import sys
import threading
import time
import urllib.parse

HOST = sys.argv[1]
IFACE = os.environ.get("STICK_IFACE", "").encode()
TIMEOUT = 10.0

states = {}
cv = threading.Condition()


def connect(timeout):
    s = socket.socket()
    if IFACE:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE, IFACE)
    s.settimeout(timeout)
    s.connect((HOST, 80))
    return s


def reader():
    s = connect(5)
    s.sendall(f"GET /events HTTP/1.1\r\nHost: {HOST}\r\n\r\n".encode())
    s.settimeout(None)
    buf = b""
    while True:
        chunk = s.recv(4096)
        if not chunk:
            return
        buf += chunk
        *lines, buf = buf.split(b"\n")
        for raw in lines:
            if not raw.startswith(b"data: {"):
                continue
            try:
                d = json.loads(raw[6:])
            except ValueError:
                continue
            if "id" in d and "value" in d:
                with cv:
                    states[d["id"]] = (d["value"], time.time())
                    cv.notify_all()


def post(path, **query):
    s = connect(5)
    q = urllib.parse.urlencode(query)
    s.sendall(f"POST /{path}?{q} HTTP/1.1\r\nHost: {HOST}\r\nContent-Length: 0\r\nConnection: close\r\n\r\n".encode())
    s.recv(1024)
    s.close()


def same(want, got):
    if isinstance(want, bool) or isinstance(want, str):
        return got == want
    try:
        g = float(got)
    except (TypeError, ValueError):
        return False
    return math.isclose(g, want, rel_tol=1e-4, abs_tol=1e-9)


def await_state(eid, want, since):
    deadline = time.time() + TIMEOUT
    with cv:
        while True:
            v = states.get(eid)
            if v is not None and v[1] >= since and same(want, v[0]):
                return True
            left = deadline - time.time()
            if left <= 0:
                return False
            cv.wait(left)


failures = []


def check(desc, ok):
    print(f"  {'ok  ' if ok else 'FAIL'}  {desc}", flush=True)
    if not ok:
        failures.append(desc)


def set_and_expect(domain, obj, value, expect=None):
    eid = f"{domain}-{obj}"
    since = time.time()
    if domain == "number":
        post(f"number/{obj}/set", value=value)
    elif domain == "select":
        post(f"select/{obj}/set", option=value)
    elif domain == "switch":
        post(f"switch/{obj}/{'turn_on' if value else 'turn_off'}")
    want = value if expect is None else expect
    check(f"{obj} = {value}", await_state(eid, want, since))
    return since


def expect(domain, obj, want, since):
    check(f"  then {obj} reads {want}", await_state(f"{domain}-{obj}", want, since))


threading.Thread(target=reader, daemon=True).start()
# The first events are the full state dump.
with cv:
    cv.wait_for(lambda: "binary_sensor-dge_connected" in states, timeout=TIMEOUT)
if states.get("binary_sensor-dge_connected", (False,))[0] is not True:
    print("generator not connected")
    sys.exit(2)
initial = {k: v[0] for k, v in states.items()}

NUMBERS = {
    "dge_frequency": 1234.5,
    "dge_amplitude": 2.5,
    "dge_offset": 0.25,
    "dge_phase": 90,
    "dge_ramp_symmetry": 30,
    "dge_pulse_duty": 30,
    "dge_pulse_width": 150.5,
    "dge_burst_period": 0.25,
    "dge_am_depth": 55,
    "dge_am_frequency": 150,
    "dge_fm_deviation": 100,
    "dge_fm_frequency": 60,
    "dge_fsk_hop_frequency": 250,
    "dge_fsk_rate": 120,
    "dge_sweep_time": 2,
    "dge_sweep_start": 200,
    "dge_sweep_stop": 2000,
    "dge_burst_cycles": 3,
}

print("numbers")
for obj, v in NUMBERS.items():
    set_and_expect("number", obj, v)

print("waveform")
for opt in ["Square", "Pulse", "Ramp", "Noise", "DC", "Sine"]:
    set_and_expect("select", "dge_waveform", opt)

print("selects")
set_and_expect("select", "dge_am_shape", "Ramp")
set_and_expect("select", "dge_fm_shape", "Square")
set_and_expect("select", "dge_sweep_spacing", "Log")
set_and_expect("select", "dge_burst_mode", "Gated")

print("exclusive modes")
# AM applied late on the unit: the read-back must still settle on AM.
since = set_and_expect("select", "dge_modulation", "AM")
since = set_and_expect("select", "dge_modulation", "FM")
since = set_and_expect("switch", "dge_sweep", True)
expect("select", "dge_modulation", "Off", since)
since = set_and_expect("switch", "dge_burst", True)
expect("switch", "dge_sweep", False, since)
set_and_expect("switch", "dge_burst", False)

print("load and output")
# A set to the value already held produces no state event, so go both ways.
since = set_and_expect("switch", "dge_load_high-z", False)
expect("number", "dge_load", 50, since)
set_and_expect("switch", "dge_load_high-z", True)
set_and_expect("switch", "dge_output", True)
set_and_expect("switch", "dge_output", False)

print("restore")
for obj in NUMBERS:
    v = initial.get(f"number-{obj}")
    if v not in (None, "NA", ""):
        post(f"number/{obj}/set", value=v)
for obj in ["dge_waveform", "dge_am_shape", "dge_fm_shape", "dge_sweep_spacing", "dge_burst_mode", "dge_modulation"]:
    v = initial.get(f"select-{obj}")
    if v:
        post(f"select/{obj}/set", option=v)
for obj in ["dge_load_high-z", "dge_sweep", "dge_burst", "dge_output"]:
    v = initial.get(f"switch-{obj}")
    if v is not None:
        post(f"switch/{obj}/{'turn_on' if v else 'turn_off'}")

print(f"\n{len(failures)} failure(s)" if failures else "\nall checks passed")
sys.exit(1 if failures else 0)
