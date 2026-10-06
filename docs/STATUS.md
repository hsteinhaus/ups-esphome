# Project state

Stage 1 (board bring-up), stage 2 (UPS monitoring) and stage 3 (the move to a
StickS3) are complete and verified against the real BX950MI. Stage 4, a second
UPS speaking a different protocol, is built and running on a second StickS3 but
has not yet met its UPS. Everything below is fact established on hardware
unless it says otherwise.

## Where things are

- Repo: `https://github.com/hsteinhaus/apc-esphome` (private), `master` pushed.
- Device: `10.22.10.65`, MAC `ac:27:6e:d2:5b:b4`, hostname `apc-ups-stick`,
  running `apc-ups-stick.yaml` against the BX950MI.
- Device: `10.22.10.66`, MAC `14:c1:9f:d5:da:d8`, hostname `qx-ups-stick`,
  running `qx-ups-stick.yaml`. No UPS attached yet.
  The CoreS3 was disconnected on 2026-10-06; `apc-ups.yaml` is kept working but
  is no longer deployed.
- Toolchain: `work/venv/bin/esphome` (2026.6.5), gitignored, on the mount.
- The secrets file is user-owned. Never read, write or echo it.

## Building and flashing

```sh
work/venv/bin/esphome compile apc-ups-stick.yaml
work/venv/bin/esphome upload apc-ups-stick.yaml --device 10.22.10.65  # OTA
work/venv/bin/python tools/sticks3_deploy.py apc-ups-stick.yaml       # USB flash + verify
work/venv/bin/python tools/sticks3_deploy.py apc-ups-stick.yaml \
    --skip-flash --host 10.22.10.65                                   # verify only
```

The second board is the same, with `qx-ups-stick.yaml` and `10.22.10.66`. Its
parser has host-side tests that need no hardware:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -o work/qx_protocol_test \
    tools/qx_protocol_test.cpp components/qx_ups/qx_protocol.cpp
work/qx_protocol_test
```

Adding a **new source file** to an external component needs a CMake
reconfigure before it is compiled: `src/CMakeLists.txt` globs at configure
time, so the file is copied into the build and then links as undefined.
`touch .esphome/build/<name>/src/CMakeLists.txt` is enough.

After a container restart `~/.platformio` is wiped and PlatformIO cannot rebuild
its own environment, because this image has no `ensurepip`. Recreate it first,
or every build fails on "Failed to create virtual environment":

```sh
work/venv/bin/python -m virtualenv ~/.platformio/penv
```

Push with `git -c credential.helper='!gh auth git-credential' push origin master`
-- `~/.gitconfig` is locked in this container, so `gh auth setup-git` fails.

USB flashing is unavailable while `usb_host` owns the OTG PHY. Address the
target only by its `/dev/serial/by-id/` path; `ttyACM*` renumbers between
boards.

## Configs

| File | Purpose |
|---|---|
| `common/ups-monitor.yaml` | Board-independent half: USB host, `apc_ups`, every entity |
| `common/qx-monitor.yaml` | Board-independent half of the Megatec monitor |
| `apc-ups-stick.yaml` | Deployed: StickS3 board package plus a 240x135 layout |
| `qx-ups-stick.yaml` | Deployed on the second StickS3, awaiting its UPS |
| `apc-ups.yaml` | CoreS3 build, kept working but not deployed |
| `boards/m5stick-s3.yaml` | StickS3 package: PMIC, display, buttons; no VBUS path |
| `boards/m5stack-cores3.yaml` | CoreS3 package: PMU rails, IO expander, USB host power |
| `usb-host-probe.yaml` | Generic USB host testing, wildcard vid/pid, register dumpers |
| `cores3-bringup.yaml` | Stage-1 hardware check |
| `tools/sticks3_deploy.py` | Flash and verify a StickS3; see its docstring for the traps |
| `tools/qx_probe.py` | Host-side dialect probe for a Cypress-bridged UPS |
| `tools/qx_protocol_test.cpp` | Host-side tests for the Megatec reply parser |

## Verified on hardware

Mains loss: Online off, On Battery on, input 0 V. Discharge 100->73 %, runtime
and battery voltage tracking (13.60 V float -> 12.40 V loaded). Recovery
restores every flag. Power: 399 W measured externally vs 395-401 W reported.
Overload correctly off at 93 %.

## Detection latency

Confirmed on the display: flags lagged a mains cut, so the interrupt endpoint
was not delivering and everything came from the 10 s poll. Three causes, all
addressed:

- The endpoint address and transfer length were hardcoded (`0x81`, 16 bytes).
  ESP-IDF rejects an IN transfer whose length is not a multiple of the
  endpoint's `wMaxPacketSize`, so a wrong guess silences the endpoint with one
  log line. Both now come from the configuration descriptor, and the interface
  claimed is the one the descriptor names.
- A refused `transfer_in()` ended the subscription permanently. `loop()` now
  resubmits whenever none is outstanding.
- Polling is tiered: status reports every `status_interval` (1 s), measurements
  every `poll_interval` (10 s), device ratings once. A cycle runs one transfer
  at a time, so polling can no longer exhaust the transfer pool and take the
  interrupt endpoint's resubmit down with it.

Worst-case detection is now `status_interval` even if no push ever arrives.
Arriving pushes are logged as `push report 0x..` at DEBUG, which is how to tell
the two paths apart.

**Verified.** The endpoint delivers several reports a second -- `0x16`
PresentStatus and `0x0C` RemainingCapacity -- arriving between poll ticks, and
a mains cut is now reported almost immediately rather than on the next tick.

## Boot-loop guard

Asserting the 5 V boost on a flat battery browns out the rail, and the board
reboots into the same restored state -- a loop it cannot leave, because
sourcing VBUS also blocks charging on the same port. Observed once, with the
battery drained flat while the board sourced VBUS to the UPS. `boost_boot_limit`
consecutive boots that never reach `boost_settle_time` abandon the restore.

**The board cannot charge while `USB Host 5V` is on.** In the permanent
configuration it has no battery at all, so it cannot source VBUS either: the
interlock refuses while the port is supplied from outside, and `USB Host 5V` is
inert there by design. It remains meaningful only for a battery-equipped bench
board, or one fed through the M5Bus.

## Permanent topology, confirmed

CoreS3 batteryless on a USB-C Y splitter: the splitter supplies both the board
and the UPS, the board sources nothing, and the UPS enumerates and pushes
normally. The splitter feeding its peripheral leg was the open question; it
does.

## StickS3, second board

Now the only deployed board. Verified on hardware: `10.22.10.65`, MAC
`ac:27:6e:d2:5b:b4`,
ESP32-S3-PICO-1 (LGA56), 8MB flash, no PSRAM enabled. `tools/sticks3_deploy.py`
reports 8/8, including that the M5PM1 answered on I2C and the LCD rail came up
-- the failure that would otherwise be a dark panel and no error. With the UPS
attached it reads 226 V in, 14 % load, 73 W against a 520 W nameplate, battery
13.60 V at 100 %, and the interrupt endpoint delivers: 180 pushes of `0x16`
PresentStatus and `0x0C` RemainingCapacity in 30 s, about 200 ms apart.

The panel is documented as ST7789P3 while the build uses ESPHome's ST7789V
`T-DISPLAY` model -- same geometry and offsets, possibly a different init
sequence. It renders correctly, but if a future panel revision comes up blank or
inverted, that is the first thing to change, not the pins.

Not yet tested on this board: a mains-loss transition. The mechanism is proven
and the firmware is the one validated on the CoreS3, but the end-to-end event
has not been seen here.

Two traps worth keeping:

- `machine.bootloader()` sets a force-download flag that an ordinary reset does
  not clear, so the board returns to `boot:0x0 (DOWNLOAD)` and never runs what
  was just flashed. `esptool --after watchdog-reset` is the way out.
- Flashing it while UiFlow2 is still installed cannot be done with esptool
  alone: that firmware presents a TinyUSB CDC port, where DTR/RTS carry no
  hardware reset and merely wedge it. The deploy tool asks MicroPython instead.

Side button: single click powers on or resets, double click powers off, long
press enters download mode with the green LED flashing. A board that is simply
switched off looks exactly like one that has vanished from the bus.

## Second UPS: Megatec/Q* behind a Cypress bridge

`0665:5161` is a Cypress HID-to-serial bridge, not a HID power device. Its
report descriptor describes an opaque byte pipe, so none of `apc_ups` applies
above the transport. Commands go out as 8-byte HID output reports via
SET_REPORT (`0x21 0x09 0x0200`), replies arrive on the interrupt IN endpoint in
8-byte chunks and are assembled until a carriage return.

**Why NUT is unreliable on this chip, and why this is not.** The bridge buffers
eight input reports. NUT reads only when it wants a reply, so an unread one --
after a timeout, or any unsolicited data -- stays queued and is read as the
*next* command's answer. Every reply then arrives one command behind, silently,
until the driver restarts; upstream patches it with a drain loop
([PR #3720](https://github.com/networkupstools/nut/pull/3720)). Keeping the
interrupt endpoint permanently armed avoids the state instead of correcting it:
the buffer never fills, and a reply arriving after its command timed out is
dropped, because nothing is outstanding to attribute it to.

What the protocol does *not* carry: charge and runtime. Charge is estimated
from battery voltage against the rating in the `F` reply, and runtime is not
reported at all -- which is why the display's bottom line shows battery voltage
and temperature rather than minutes left. Watts need a plate rating set in
`common/qx-monitor.yaml`; until it is set, load percent is the headline.

Written with no access to the device, so the parser is a standalone module with
host-side tests, and every command and reply is logged at VERBOSE with its
round-trip time. That log is how the remaining unknowns get settled.

## Still open

1. **A mains-loss transition on the StickS3.** The push path is proven on it
   and the firmware is the one validated on the CoreS3, but the event itself
   has not been seen on this board.
2. **The Megatec UPS has never been attached.** Everything about `qx_ups`
   beyond the parser tests is written from the protocol. Once the stick is on
   it, the log answers: does `Q1` get a reply at all, is the dialect plain
   Megatec, what do `I` and `F` return, and what is the round trip.
3. **`nominal_power` and the battery voltage range** for that UPS are unset, so
   watts and charge are not published yet. Both come from the `I`/`F` replies.

## Upstream bugs found

Filing these was declined on 2026-10-06 -- a decision, not an oversight. They
are kept here because each one cost hours to find and each is invisible from
the outside.

- `m5stack/esphome-yaml` `axp2101` emits a `battery_charging` binary sensor
  without declaring `binary_sensor` in `AUTO_LOAD`; builds fail on a missing
  header unless some other binary sensor exists.
- ESPHome `usbh_print_intf_desc()` prints `bInterfaceProtocol` under a label
  reading `bInterfaceClass`.
- ESPHome `USBClient` never claims an interface and does not document that
  subclasses must; interrupt endpoints silently never deliver without it.
- ESPHome `max_packet_size` caps control transfers on every variant, though it
  is documented as an ESP32-P4 high-speed concern. The default 64 leaves 56
  bytes, too small for any HID report descriptor.
