# Project state

Stage 1 (CoreS3 bring-up) and stage 2 (UPS monitoring) are both complete and
verified against the real BX950MI. Everything below is fact established on
hardware, not inference.

## Where things are

- Repo: `https://github.com/hsteinhaus/apc-esphome` (private), `master` pushed.
- Device: `10.22.10.62`, hostname `apc-ups`, running `apc-ups.yaml`.
- Toolchain: `work/venv/bin/esphome` (2026.6.5), gitignored, on the mount.
- The secrets file is user-owned. Never read, write or echo it.

## Building and flashing

```sh
work/venv/bin/esphome compile apc-ups.yaml
work/venv/bin/esphome upload apc-ups.yaml --device 10.22.10.62   # OTA
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

## Still open

1. **Interrupt vs poll latency.** The mains transition landed on a 10 s poll
   boundary, so the interrupt endpoint has never been proven to deliver.
   Detection is correct either way but may be up to 10 s late, which matters if
   anything triggers a shutdown from it. Check by cycling `USB Host 5V` and
   reading the connect log for the `could not claim interface` line.
2. **`/update` browser OTA** is registered but never exercised. It is the
   recovery path once the board is at the UPS -- test it while still reachable.
3. **Upstream reports not filed** (see below).

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
