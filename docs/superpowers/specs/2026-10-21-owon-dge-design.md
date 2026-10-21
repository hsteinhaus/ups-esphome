# Owon DGE1060 signal generator on the PSU stick — design

## Intent

The lab PSU stick (`owon-psu-stick.yaml`, M5Stack StickS3) gets a second
instrument: an Owon DGE1060 function generator, on the same USB port through a
bus-powered USB 2.0 hub. Goal: full control of the generator plus read-back of
every setting, from the stick's web UI first and Home Assistant second — the
same model as `owon_psu`.

## Goals

- New ESPHome component `components/owon_dge/`, used from
  `common/psu-monitor.yaml` next to `owon_psu`.
- Every setting listed under *Entities* is settable and read back; an entity
  shows what the generator reports, never what was requested.
- PSU and generator work together behind the hub, from boot, without replugging.

## Non-goals

- No amplitude cap, no output-off on link loss (user: "keep it simple").
- No arbitrary waveform upload, no display changes, no second channel
  (the DGE1060 answers nothing on `SOUR2:`/`OUTP2?`).
- No FSK, PWM, counter, square duty: not answered on this unit (see below) —
  except FSK, which *is* answered and is in scope.

## Anti-goals

- Publishing a value the generator did not read back.
- A query that the unit never answers in the normal poll set (it leaves a bulk
  IN pending and invites reply misalignment).
- Lucky-timing fixes (sleeps/retries) for the boot race — find and fix the cause.

## Facts established on hardware (2026-10-21)

- **USB:** VID:PID 5345:1234, full speed via the hub. One interface (#0,
  class 0x00), bulk OUT 0x01, bulk IN 0x81. Device descriptor claims class 0x03.
- **Descriptor bug:** both bulk endpoints report `wMaxPacketSize 512` (a
  high-speed value) at full speed. ESP-IDF refuses to claim the interface
  (`EP MPS (512) exceeds supported limit (408)` → `ESP_ERR_NOT_SUPPORTED`).
  Clamping the cached config descriptor's endpoint MPS to 64 before
  `usb_host_interface_claim` works (Linux does the same clamp). Proven in the
  throwaway spike `components/dge_probe/`.
- **Protocol:** raw SCPI text, command + `\n` out on 0x01; reply line ending
  `\n` (strip `\r`) on 0x81, delivered in ≤64-byte IN transfers (a 46-byte
  `*IDN?` reply arrives in one). Sets never reply. Unknown commands never reply
  (and set the sticky `SYSTem:ERRor?` to -102, which is not cleared by reading
  — do not poll it).
- `*IDN?` → `OWON,DGE1060,24500065,SCPI:99.0 FV:V1.0.4.4.0`.
- Reference: Owon *DGE2000/3000 SCPI Programmer Manual* V1.0.1 —
  https://files.owon.com.cn/software/Application/DGE2000_3000_SCPI_Protocol.pdf
  (needs browser UA + `Referer: https://www.owon.com.hk/`). Prior art:
  https://github.com/hamsternz/owon_dge20xx_linux (libusb, same endpoints).

### Queries verified to answer on this unit (prefix `SOUR1:` unless noted)

| Query | Example reply | Entity |
|---|---|---|
| `OUTP1?` | `0` | Output switch |
| `OUTP1:IMP?` | `9.9E+37` (= High-Z) | Load (number Ω; ≥9.9e37 shown as High-Z) |
| `FUNC:SHAP?` | `SIN` | Waveform select: SIN SQU PUL RAMP PRN |
| `FREQ?` | `3.200000e+02` | Frequency (Hz) |
| `VOLT?` | `6.000000e+00` | Amplitude (Vpp) |
| `VOLT:OFFS?` | `0.000000e+00` | Offset (V) |
| `PHAS?` | `0.000000e+00` | Phase (rad on the wire; entity in °) |
| `FUNC:RAMP:SYMM?` | `50.0` | Ramp symmetry (%) |
| `PULS:DCYC?` | `20.0` | Pulse duty (%) |
| `PULS:WIDT?` | `2.247191e-04` | Pulse width (s) |
| `PULS:TRAN?` / `PULS:TRAN:TRA?` | `6.734000e-06` | Pulse lead / trail edge (s) |
| `AM:STAT?` `FM:STAT?` `FSK:STAT?` | `0` | → one Modulation select: OFF AM FM FSK (PM: see below) |
| `AM:DEPT?` | `100` | AM depth (%) |
| `FM:DEV?` | `2.2e+02` | FM deviation (Hz) |
| `PM:DEV?` | `0` | PM deviation (rad on wire; entity in °) |
| `AM:SOUR?` / `FM:SOUR?` / `FSK:SOUR?` | `INT` | Per-type source select INT/EXT |
| `AM:INT:FREQ?` / `FM:INT:FREQ?` | `1e+02` | Per-type modulation frequency (Hz) |
| `AM:INT:FUNC?` / `FM:INT:FUNC?` | `SIN` | Per-type modulation shape select |
| `FSK:FREQ?` / `FSK:INT:RATE?` | `1e+02` | FSK hop frequency / rate (Hz) |
| `SWE:STAT?` `SWE:SPAC?` `SWE:TIME?` `SWE:SOUR?` | `0` `LIN` `1` `TIM` | Sweep on, LIN/LOG, time (s), source TIM/MAN/EXT |
| `FREQ:STAR?` `FREQ:STOP?` | `1e+02` `1e+03` | Sweep start/stop (Hz) |
| `BURS:STAT?` `BURS:MODE?` `BURS:NCYC?` `BURS:INT:PER?` `BURS:SOUR?` `BURS:GATE:POL?` | `0` `TRIG` `1` `2.17e+04` `TIM` `POS` | Burst on, TRIG/GAT, cycles, period (s), source, gate polarity |

Not answered (exclude): `FUNC?`, `VOLT:UNIT?`, `SQU:DCYC?`, `FUNC:SQU:DCYC?`,
`FUNC:PULS:*`, `PER?`, `OUTP1:LOAD?`, `OUTP1:POL?`, `MODE:STAT?`, `MOD:TYPE?`,
`PWM:*`, `BURS:PHAS?`, `COUN:FREQ?`. (`MOD:STAT?` answers `OFF` but is
redundant with the per-type states.)

**Verified sets (2026-10-21, output unconnected):** sets never reply.
- Shape tokens: `SIN SQU PULS RAMP PRN DC` (read back the same; `PULS`, not
  the manual's `PUL`; long forms like `SINusoid` accepted). Include DC.
- Modulation INT function token is `SQU` (the manual's `SUQ` is ignored).
- `OUTP1 ON|OFF` → `OUTP1?` `1|0`.
- AM, FM, FSK, sweep and burst are **mutually exclusive**: enabling one turns
  the active one off.
- **Apply latency:** `AM:STAT ON` read back `0` immediately and `1` two queries
  later. Confirmation must therefore re-query an unconfirmed set until it
  matches (as `owon_psu` already does), not judge on the first read-back.
- `PM:STAT ON` never took (read back `0`). Treat PM as unsupported on this
  unit unless the builder finds the precondition; do not expose it otherwise.

**Still unverified:** value ranges on the 60 MHz model, the unit of
`BURS:INT:PER`.

## Decisions (user)

- Full control + read-back of every setting.
- Same entity/read-back model as the PSU.
- No safety features for now.
- Lives on the existing PSU stick behind the hub.

## Decisions (mine — reviewable)

- **Transport inside `owon_dge`:** the component *is* a `usb_host::USBClient`
  subclass (like the spike), not a uart. Clamp MPS to 64 in `on_connected()`
  before claiming interface 0. No generic bulk-uart layer — one user only.
- **Engine mirrors `owon_psu`:** single query in flight, reply timeout,
  resync on `*IDN?` after a miss, set-once-then-requery confirmation, status
  poll (output, shape, freq, ampl, offset; ~1 s) and a round-robin over the
  rest (~one query per 250 ms). Only queries for the *active* modulation type
  and enabled sweep/burst are polled frequently; others round-robin.
- **Entity mapping:** ESPHome `select` for enumerated values, `number` for
  numeric, `switch` for output/sweep/burst, read-only state for identity.
  Phase/PM deviation shown in degrees, converted at the edge.
- **Web UI groups:** Signal, Modulation, Sweep, Burst (plus existing PSU/Caps/Board).
- **OTA:** `suspend_usb()`/`resume_usb()` like `owon_psu` (same flash-erase
  hazard); add both components to the existing `on_begin`/`on_error` lambdas.

## Related fixes in the same effort

1. **USB boot race (needs reproduction first).** `usb_host`'s client accepts a
   new-device address only while idle; while it is opening/closing another
   device, the NEW_DEV events for the rest are dropped and never revisited.
   Behind a hub, devices arrive in a burst. One boot with hub support failed to
   find the PSU until a replug; later boots succeeded. Reproduce by rebooting
   repeatedly (raise the odds, e.g. more devices or slower client handling),
   then fix: when a client returns to INIT, enumerate present devices
   (`usb_host_device_addr_list_fill`) and open the first unclaimed match.
   `usb_host` is not vendored; `usb_uart` is — apply the fix in our clients
   (vendored `usb_uart`, new `owon_dge`) rather than vendoring `usb_host`,
   unless that proves impossible.
2. **PSU resync bug.** After a missed reply, `owon_psu` resyncs on the first
   IDN-shaped reply, but a second, late `*IDN?` reply can still be in flight and
   then shifts every following reply by one (observed: `MEAS:ALL:INFO? -> IDN
   text`). Fix: resync must leave no earlier `*IDN?` unanswered — e.g. count
   outstanding IDN queries and drop IDN replies until the count reaches zero,
   or tag-free equivalent. It self-healed in the observed case; still a bug.
3. Already uncommitted: `usb_host: enable_hubs: true` (keep) and temporary
   DEBUG logging + `dge_probe` spike (remove).

## Builder-check answers (mine)

- **Per-type modulation entities**, not "of the active type": AM depth/freq/
  shape/source, FM deviation/freq/shape/source, FSK hop freq/rate/source each
  get their own entity, always polled (round-robin) and always settable,
  independent of which type is on. No ambiguity when modulation is OFF, and
  every value shown is a read-back.
- **Spike's 5 s `on_connected()` retry** was a diagnostic only (to re-log the
  claim error after boot). The claim failed deterministically on MPS 512;
  with the clamp, the first claim succeeds. `owon_dge` claims once, no retry.
- **Boot race, bounded:** the dropped-NEW_DEV path is a logic flaw visible in
  `usb_host_client.cpp` (`on_opened()` ignores addresses unless INIT, and
  nothing revisits them). Try to reproduce with 20 logged reboots; whether or
  not it reproduces, fix the logic (enumerate present devices on return to
  INIT) — it is a reasoned fix of a proven code path, not a timing dodge.
  Verify with 20 reboots, both clients (PSU, DGE) attaching every time.
- **PSU resync must be bounded:** replies can be lost outright (the 3-miss
  path exists), so no "count outstanding IDN" scheme. Instead: after a
  timeout, stop sending and discard RX until the line has been silent for one
  reply timeout (event: no byte arrived in that window), then send exactly one
  `*IDN?`. No earlier reply can then arrive after the resync IDN.

## Left to the builder

File layout inside `owon_dge`, exact poll intervals, entity names/icons, how
the modulation-type select maps onto the per-type `STAT` commands, number
min/max/step (take from the manual and confirm on the unit), and test
procedure — following the repo's existing verification style (live entities
over the web API, `tools/sticks3_deploy.py` where it applies).

## Environment notes

- Nothing is connected to the generator output: live tests may change any
  setting and toggle the output.

- Host's wired net (10.22.70.x) cannot reach the sticks' 10.22.10.x; use Wi-Fi
  (`curl --interface wlp0s20f3`, web OTA at `/update`) — `esphome run` OTA
  cannot pick the interface.
