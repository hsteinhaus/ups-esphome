# Owon bench stick: guide for automated HIL use

This is for an agent (or a script) that drives the bench instruments in
hardware-in-the-loop runs. One M5Stack StickS3 (`owon-psu-stick`) bridges
both instruments to the network:

| Instrument | What | Entity prefix |
|---|---|---|
| Owon SPE6103 | lab PSU, 0-60 V / 0-10 A, 1 channel | `psu_` |
| Owon DGE1060 | function generator, 60 MHz, 1 channel | `dge_` |

The stick also serves this file at **`http://<stick>/agent.md`**, matching
the running firmware. The `text_sensor/agent_guide` entity, in the web UI's
Board group, points there.

## Contract

1. **Every value is a read-back.** An entity shows what the instrument
   reports, never what was asked for. So a set is confirmed only when the
   entity reports the new value.
2. **A set returns before the instrument applies it.** HTTP 200 means the
   stick took the request, not that the instrument applied it.
   - Out-of-range values also get 200 and are dropped.
   - So are sets above a PSU cap, and sets while the instrument is
     disconnected.
   - Always confirm by read-back (see [Confirming a set](#confirming-a-set)).
3. **Check `*_connected` first.** While `binary_sensor/psu_connected` or
   `binary_sensor/dge_connected` is off, that instrument's values are stale
   and its sets are dropped.
4. **Leave the bench safe.** At the end of a run, and on any failure path,
   turn both outputs off.
   - Nothing turns the PSU off when a web client goes away. The link-loss
     trip watches only the Home Assistant API.
   - Nothing ever turns the DGE off on its own.

## Reaching the stick

- **Base URL:** `http://owon-psu-stick.local/` (mDNS) or its IP. The bench
  stick has been at `10.22.10.83`.
- There is no auth and no TLS. Plain HTTP on port 80.
- If the host has several networks, only one may route to the stick. In that
  case, bind to that interface: `curl --interface <if>`, or
  `SO_BINDTODEVICE` from Python.

## HTTP API (ESPHome web_server v3)

The object id is the entity name in lower case, with spaces turned into `_`:
`DGE Frequency` becomes `dge_frequency`. The URL-encoded name also works:
`/number/DGE%20Frequency`.

| Action | Request |
|---|---|
| Read | `GET /<domain>/<object_id>` |
| Set number | `POST /number/<id>/set?value=<v>` |
| Set select | `POST /select/<id>/set?option=<Option>` |
| Switch | `POST /switch/<id>/turn_on` or `/turn_off` |
| Press button | `POST /button/<id>/press` |
| Live states + log | `GET /events` (Server-Sent Events) |

A POST must carry `Content-Length: 0`, or it gets HTTP 411. With curl, use
`curl -d '' ...`, not `curl -X POST ...`.

A read returns JSON like this:

```json
{"name_id":"number/DGE Frequency","id":"number-dge_frequency","value":"1234.500","state":"1234.500 Hz"}
```

- **`value`:**
  - For numbers it is a string, rounded to the entity's step.
  - For switches and binary sensors it is a bool.
  - For selects and text sensors it is a string.
  - An unknown number reads `"NaN"`; its `state` is `NA`.
- **`/events`:** opens with one `state` event per entity, which is the full
  snapshot. After that it sends one event per change, as `event: state` /
  `data: {...}`.
  - `event: log` carries the stick's INFO and WARN log lines. These include
    refusals such as `... refused: above the ... cap` and
    `... set not taken, reads ...`.
  - `event: ping` is a keepalive.
- **No event for an unchanged value.** A set that leaves the value where it
  was produces no state event. Don't wait for an event in that case; read
  the value with GET.

### Confirming a set

```python
# Python 3, stdlib only. Watch /events in a thread, keep latest[id] = (value, t).
def set_and_confirm(path, query, eid, want, timeout=5.0):
    t0 = time.time()
    post(path, **query)                      # e.g. "number/psu_voltage_setpoint/set", value=5
    if wait_for(eid, want, since=t0, timeout=timeout):   # SSE state event matched
        return True
    return close(get(eid)["value"], want)     # unchanged value sends no event: GET it
```

`tools/owon_dge_check.py` in the repo is a complete working client. It
contains the SSE reader, POST and wait-for-value, and is a good template.

### How long to wait

| Instrument | Values | Updates within |
|---|---|---|
| PSU | V, I, P, output, mode, protection flags | 0.5 s |
| PSU | setpoints, OVP/OCP limits | 5 s (re-read cycle); a set is confirmed sooner |
| DGE | output, waveform, freq, ampl, offset, modulation, sweep, burst | 1 s |
| DGE | everything else | re-queried right after a set until it matches, else on the 5 s cycle |

5 s is a safe timeout for any set. If the instrument still disagrees after
5 s, it refused the value. The entity keeps the real value, and the log
says `set not taken`.

The PSU sometimes takes about 1 s to answer just after an output change.

## PSU entities (SPE6103)

| Entity | Domain | Unit | Notes |
|---|---|---|---|
| `psu_output` | switch | | Output on/off |
| `psu_voltage_setpoint` | number | V | 0-60 V in 0.01 V steps. Refused above `psu_voltage_cap` |
| `psu_current_setpoint` | number | A | 0-10 A in 0.01 A steps. Refused above `psu_current_cap` |
| `psu_ovp_limit` | number | V | Over-voltage protection, 0-62 V |
| `psu_ocp_limit` | number | A | Over-current protection, 0-10.2 A |
| `psu_voltage_cap` | number | V | The stick's own bound on the setpoint. Kept across reboots |
| `psu_current_cap` | number | A | As above, for current |
| `psu_voltage` | sensor | V | Measured |
| `psu_current` | sensor | A | Measured |
| `psu_power` | sensor | W | Measured |
| `psu_mode` | text_sensor | | `CV`, `CC`, or `mode 0` while the output is off |
| `psu_ovp_tripped` | binary_sensor | | Protection tripped |
| `psu_ocp_tripped` | binary_sensor | | Protection tripped |
| `psu_otp_tripped` | binary_sensor | | Protection tripped |
| `psu_connected` | binary_sensor | | The PSU is answering |
| `psu_model` | text_sensor | | From `*IDN?` |
| `psu_serial_number` | text_sensor | | From `*IDN?` |
| `psu_firmware` | text_sensor | | From `*IDN?` |

**Caps** are the stick's guard, separate from the PSU's own OVP and OCP.
Before a run, set the caps to the device under test's limits; a mistyped
setpoint is then refused instead of applied.

- The caps are the operator's to set, not the agent's.
- Do not raise them unless your task says to.

Protection trips, and CC mode under a real load, have not been checked
against a load yet. Treat `*_tripped` as advisory and cross-check with
`psu_voltage` and `psu_current`.

## DGE entities (DGE1060, channel 1)

**Signal**

| Entity | Domain | Range / options |
|---|---|---|
| `dge_output` | switch | on/off |
| `dge_waveform` | select | `Sine` `Square` `Pulse` `Ramp` `Noise` `DC` |
| `dge_frequency` | number | 0.001 Hz - 60 MHz in 1 mHz steps; the generator's own limit depends on the waveform |
| `dge_amplitude` | number | 0.001-20 Vpp |
| `dge_offset` | number | -10 to 10 V |
| `dge_phase` | number | 0-360 ° |
| `dge_ramp_symmetry` | number | 0-100 % (Ramp) |
| `dge_pulse_duty` | number | 0-100 % (Pulse) |
| `dge_pulse_width` | number | µs (Pulse) |
| `dge_pulse_leading_edge` | number | µs (Pulse) |
| `dge_pulse_trailing_edge` | number | µs (Pulse) |
| `dge_load_high-z` | switch | on = High-Z; off = 50 Ω |
| `dge_load` | number | Ω, 1-10000. Reads `NaN` while High-Z |

Amplitude and offset are given for the load setting. Set the load first,
then the levels.

**Modulation, sweep, burst.** At most one of these runs at a time. Starting
one stops the others. Confirm the side effects by read-back, as
`tools/owon_dge_check.py` does.

| Entity | Domain | Range / options |
|---|---|---|
| `dge_modulation` | select | `Off` `AM` `FM` `FSK`. AM can take a moment longer to read back |
| `dge_am_depth` | number | 0-100 % |
| `dge_am_frequency` | number | 2 mHz - 100 kHz |
| `dge_am_shape` | select | `Sine` `Square` `Ramp` `Noise` |
| `dge_am_source` | select | `Internal` `External` |
| `dge_fm_deviation` | number | Hz |
| `dge_fm_frequency` | number | 2 mHz - 100 kHz |
| `dge_fm_shape` | select | Same options as `dge_am_shape` |
| `dge_fm_source` | select | Same options as `dge_am_source` |
| `dge_fsk_hop_frequency` | number | Hz |
| `dge_fsk_rate` | number | 2 mHz - 100 kHz |
| `dge_fsk_source` | select | `Internal` `External` |
| `dge_sweep` | switch | on/off |
| `dge_sweep_start` | number | Hz |
| `dge_sweep_stop` | number | Hz |
| `dge_sweep_time` | number | s |
| `dge_sweep_spacing` | select | `Linear` `Log` |
| `dge_sweep_source` | select | `Timer` `Manual` `External` |
| `dge_burst` | switch | on/off |
| `dge_burst_mode` | select | `Triggered` `Gated` |
| `dge_burst_cycles` | number | 1-500000 |
| `dge_burst_period` | number | s |
| `dge_burst_source` | select | `Timer` `Manual` `External` |
| `dge_burst_gate_polarity` | select | `Positive` `Negative` |

**Info:** `dge_connected` (binary_sensor), plus the text sensors
`dge_model`, `dge_serial_number` and `dge_firmware`.

**Not available:**
- PM: the unit never enables it.
- Arbitrary waveform upload.
- A second channel.
- A software trigger for `Manual` sources.

## Recipes

Set `S=http://owon-psu-stick.local`.

**5 V / 100 mA supply, then on:**

```sh
curl -sd '' "$S/switch/psu_output/turn_off"
curl -sd '' "$S/number/psu_current_setpoint/set?value=0.1"
curl -sd '' "$S/number/psu_voltage_setpoint/set?value=5"
# confirm both setpoints read back before:
curl -sd '' "$S/switch/psu_output/turn_on"
curl -s "$S/sensor/psu_voltage"     # wait for it to settle near 5
```

**1 kHz, 3.3 V logic square into High-Z:**

```sh
curl -sd '' "$S/switch/dge_output/turn_off"
curl -sd '' "$S/switch/dge_load_high-z/turn_on"
curl -sd '' "$S/select/dge_modulation/set?option=Off"
curl -sd '' "$S/switch/dge_sweep/turn_off"
curl -sd '' "$S/switch/dge_burst/turn_off"
curl -sd '' "$S/select/dge_waveform/set?option=Square"
curl -sd '' "$S/number/dge_frequency/set?value=1000"
curl -sd '' "$S/number/dge_amplitude/set?value=3.3"
curl -sd '' "$S/number/dge_offset/set?value=1.65"
# confirm all of the above, then:
curl -sd '' "$S/switch/dge_output/turn_on"
```

**Teardown, always:**

```sh
curl -sd '' "$S/switch/dge_output/turn_off"
curl -sd '' "$S/switch/psu_output/turn_off"
```

## Run hygiene

- **One controller at a time.** The stick does not arbitrate. Two agents,
  or an agent plus a person at the web UI, will overwrite each other.
  - Before the run, record the starting state from the first `/events`
    snapshot.
  - Afterwards, restore what you changed.
- **Pace your requests.** Send them one at a time and wait for each
  response. The web server handles requests in series, and each SCPI set
  takes a few tens of ms on the bus. Do not fire hundreds of requests in a
  burst.
- **Firmware updates stop the instruments.** An OTA update pauses both
  instruments' USB, and the stick then reboots. Never update during a run.
- **`button/restart`** reboots the stick. The instruments keep their
  settings and their output state; the stick re-reads them after boot.
- **When `*_connected` stays off:**
  - Check the bus-powered hub and its cables.
  - Press `button/restart`; the bus is enumerated again at boot.
  - The DGE must be in its "PC" USB mode, which is the front-panel default.
