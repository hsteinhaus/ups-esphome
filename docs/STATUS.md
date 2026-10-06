# Project state

Stage 1 (CoreS3 bring-up) and stage 2 (UPS monitoring) are both complete and
verified against the real BX950MI. Everything below is fact established on
hardware, not inference.

## Where things are

- Repo: `https://github.com/hsteinhaus/apc-esphome` (private), `master` pushed.
- Device: `10.22.10.64`, hostname `apc-ups`, running `apc-ups.yaml`.
- Toolchain: `work/venv/bin/esphome` (2026.6.5), gitignored, on the mount.
- The secrets file is user-owned. Never read, write or echo it.

## Building and flashing

```sh
work/venv/bin/esphome compile apc-ups.yaml
work/venv/bin/esphome upload apc-ups.yaml --device 10.22.10.64   # OTA
tools/cores3.sh upload apc-ups.yaml                              # USB, by-id
```

Push with `git -c credential.helper='!gh auth git-credential' push origin master`
-- `~/.gitconfig` is locked in this container, so `gh auth setup-git` fails.

USB flashing is unavailable while `usb_host` owns the OTG PHY. Address the
target only by its `/dev/serial/by-id/` path; `ttyACM*` renumbers between
boards.

## Configs

| File | Purpose |
|---|---|
| `apc-ups.yaml` | Production: UPS entities, display, web UI |
| `usb-host-probe.yaml` | Generic USB host testing, wildcard vid/pid, register dumpers |
| `cores3-bringup.yaml` | Stage-1 hardware check |
| `boards/m5stack-cores3.yaml` | Shared board package |

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

Flashed and verified on hardware: `10.22.10.65`, MAC `ac:27:6e:d2:5b:b4`,
ESP32-S3-PICO-1 (LGA56), 8MB flash, no PSRAM enabled. `tools/sticks3_deploy.py`
reports 8/8, including that the M5PM1 answered on I2C and the LCD rail came up
-- the failure that would otherwise be a dark panel and no error.

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

## Still open

1. **`/update` browser OTA** is registered but never exercised. It is the
   recovery path once the board is at the UPS -- test it while still reachable.
1. **Upstream reports not filed** (see below).

## Upstream bugs found

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
