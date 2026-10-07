# Project state

Stage 1 (board bring-up), stage 2 (UPS monitoring) and stage 3 (the move to a
StickS3) are complete and verified against the real BX950MI. Stage 4, a second
UPS speaking a different protocol, is built and running on a second StickS3 but
has not yet met its UPS. Everything below is fact established on hardware
unless it says otherwise.

## Where things are

- Repo: `https://github.com/hsteinhaus/ups-esphome` — **public, GPLv2**,
  `master` pushed. Renamed from `apc-esphome` on 2026-10-06 once it covered
  two UPS protocols; GitHub redirects the old URL. The working directory is
  still `/workspace/apc-esphome`: it is a bind mount, renameable only from the
  host.
- Device: `10.22.10.65`, MAC `ac:27:6e:d2:5b:b4`, hostname `apc-ups-stick`,
  running `apc-ups-stick.yaml` against the BX950MI.
- Device: `10.22.10.66`, MAC `14:c1:9f:d5:da:d8`, hostname `qx-ups-stick`,
  running `qx-ups-stick.yaml` against an ABB PowerValue 11 RT G2 1 kVA.
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
| `common/apc-monitor.yaml` | Board-independent half of the APC monitor |
| `common/qx-monitor.yaml` | Board-independent half of the Megatec monitor |
| `apc-ups-stick.yaml` | Deployed: StickS3 board package plus a 240x135 layout |
| `qx-ups-stick.yaml` | Deployed on the second StickS3, on the ABB PowerValue |
| `apc-ups.yaml` | CoreS3 build, kept working but not deployed |
| `boards/m5stick-s3.yaml` | StickS3 package: PMIC, display, buttons; no VBUS path |
| `boards/m5stack-cores3.yaml` | CoreS3 package: PMU rails, IO expander, USB host power |
| `usb-host-probe.yaml` | Generic USB host testing, wildcard vid/pid, register dumpers |
| `cores3-bringup.yaml` | Stage-1 hardware check |
| `tools/sticks3_deploy.py` | Flash and verify a StickS3; see its docstring for the traps |
| `tools/qx_probe.py` | Host-side dialect probe for a Cypress-bridged UPS |
| `tools/qx_protocol_test.cpp` | Host-side tests for the Megatec reply parser |
| `tools/force_safe_mode.sh` | Recover a board whose firmware crashes during OTA |

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
round-trip time.

### What the real device turned out to be

Attached 2026-10-06. It is an **ABB PowerValue 11 RT G2, 1 kVA** -- an online
double-conversion unit. Nothing on the USB id says so; the UPS named itself in
its `I` reply, as `WPHVR1K0` / firmware `01072.06`. There is a NUT mailing-list
thread for this exact model titled "blazer_usb works / nutdrv_qx iffy", which
matches the reliability complaint that started this.

Everything written blind worked unchanged on first contact. `Q1` parsed, the
round trip is a steady **370-420 ms**, and no desync appeared across any run.

Three things only the device could tell us:

- **It reports battery voltage per cell, not per pack**: `2.27` against a 24 V
  rating, which is one of twelve cells at float charge. Taken literally it read
  as a flat battery -- 0 % on a healthy pack. The reading is now scaled by the
  cell count the `F` rating implies, so it publishes 27.24 V and 100 %.
- **The manufacturer field is blank.** That is the UPS, not the parser; model
  and firmware come through from the same reply.
- `F` answers `230.0 V, 4 A, 24.00 V battery, 50 Hz`. Those three are published
  as diagnostic sensors, because the exchange happens within a second of
  enumeration -- long before a log client can attach to watch it. That is the
  only practical way to read them.

`nominal_power` is set to **900 W** (1000 VA at power factor 0.9) in
`qx-ups-stick.yaml`. Megatec reports load only as a percentage, so watts are
load x that figure -- the same convention NUT and apcupsd use, and no better
than the plate value it is given.

### Detection latency, and its floor

This UPS exists to buffer brief switching interruptions, so the events worth
seeing can be shorter than one poll. The protocol has no push and no event
latch, and a `Q1` round trip measures 350-405 ms, so there is a hard floor.
`status_interval` is **500 ms**, measured at a 0.510 s mean gap over 42 polls
with no timeouts, no unparsed replies and no late replies -- the bridge takes
double the old rate without desyncing. Worst-case detection is about 0.9 s,
down from 1.4 s. **An interruption shorter than roughly half a second can
still pass entirely unseen**, and no amount of tuning fixes that: it is the
protocol, not the implementation.

That forced the scheduler order. Status-first would starve the nameplate
queries once the interval drops below one round trip, and the `F` rating is
not optional -- without it the battery voltage publishes per cell as though it
were the pack. `I` and `F` now go first and only at startup, with bounded
retries, and status owns everything afterwards.

## Still open

1. **A mains-loss transition on the StickS3.** The push path is proven on it
   and the firmware is the one validated on the CoreS3, but the event itself
   has not been seen on this board.
2. **A mains-loss transition on the ABB.** Steady-state readings are all
   confirmed against the device, but no transition has been observed: the
   `utility fail` bit, the latched input fault voltage and the discharge curve
   are still only as good as the protocol says.
3. **Whether the 2026-10-07 teardown was the flash-window fault.** At uptime
   2315 s the APC board's device vanished and re-enumerated without a reboot.
   The same day, OTA on that board was found to crash in `esp_ota_begin`, root
   caused below, and a port event is the gentler outcome of the same starvation.
   That is a strong lead, not a proof: nothing in the log ties that particular
   teardown to a flash write. The reconnect gap now logged will classify the
   next one -- a port error re-enumerates a still-attached device in a few
   hundred ms, a real disconnect takes as long as the cable does.

## The USB host ISR cannot survive a flash erase

The decisive find of 2026-10-07, and the reason OTA stopped working.

The ESP-IDF USB host ISR is flash-resident and the whole USB-OTG Kconfig menu
offers **no IRAM option**. While a flash erase has the cache off, that ISR
cannot run, but the DWC controller keeps going. When interrupts come back the
ISR finds the buffer state past its own bookkeeping and asserts:

```
Fault - IllegalInstruction, core 1
panic_abort <- __assert_func <- hcd_dwc _buffer_parse <- _xt_lowint1
  <- esp_intr_noniram_enable <- spi_flash_op_block_func <- ipc_task
  (from esphome::ota::IDFOTABackend::begin)
```

With an interrupt endpoint armed continuously -- which both components do on
purpose -- a transfer landing inside the erase window is a certainty, not a
chance. Both OTA platforms in both configs now call `suspend_usb()` on_begin:
halt the endpoint, flush it, and wait for an in-flight command to land, so
nothing is outstanding when the cache goes away. `on_error` resumes.

**The hazard scales with the cache-off window, and routine NVS writes are
below it.** Measured 2026-10-07 on the ABB board: `flash_write_interval: 1s`
plus a persisting backlight, 51 real `Writing 1 items` preference writes with
the endpoint armed, zero faults and `Q1` never missed. A 4 KB page write
survives; the ~1.1 MB `esp_ota_begin` erase does not. So preference saves need
no guard, but anything that erases in bulk does.

**Recovering a board that predates the fix:** its firmware crashes on every
OTA attempt, so it cannot receive the fix. Each attempt crashes it inside
`boot_is_good_after` (60 s), so the safe_mode boot-loop counter climbs; after
`num_attempts` (10) it boots into safe mode, where `usb_host` never starts and
the OTA lands. `tools/force_safe_mode.sh <config.yaml> <host>` does this.
Done once, on the APC board, 2026-10-07.

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
