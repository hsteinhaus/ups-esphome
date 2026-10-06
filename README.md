# ups-esphome

ESPHome components to read a UPS over USB from an M5Stack board, with status on
the built-in display. Two UPSes, and they share almost nothing above the wire:

- **APC Back-UPS BX950MI** (`051d:0002`) is a real USB HID Power Device.
  `apc_ups` parses its report descriptor at runtime and resolves every value by
  HID usage path, so it never depends on a vendor's report layout.
- A **Megatec/Q\* UPS behind a Cypress HID-to-serial bridge** (`0665:5161`) only
  looks like a HID device. Its descriptor describes an opaque byte pipe, and the
  UPS behind it answers ASCII commands. `qx_ups` tunnels those through 8-byte
  HID reports.

What they do share is the transport work underneath: endpoint discovery from the
configuration descriptor, the interface claim ESPHome's `USBClient` omits, and a
permanently armed interrupt endpoint.

## Layout

| Path | Purpose |
|---|---|
| `common/apc-monitor.yaml` | Board-independent half of the APC monitor: USB host, `apc_ups`, all entities |
| `common/qx-monitor.yaml` | Board-independent half of the Megatec monitor: USB host, `qx_ups`, all entities |
| `apc-ups.yaml` | CoreS3 build: board package plus a 320x240 layout |
| `apc-ups-stick.yaml` | StickS3 build: board package plus a 240x135 layout |
| `qx-ups-stick.yaml` | Second StickS3, Megatec UPS: board package plus a 240x135 layout |
| `boards/m5stack-cores3.yaml` | Board package: PMU rails, IO expander, display, touch, USB host power |
| `boards/m5stick-s3.yaml` | Board package: PMIC, display, buttons -- no USB host power path |
| `cores3-bringup.yaml` | Stage-1 test config: display, touch and PMU telemetry |

A board config contributes its hardware and a display layout sized to its panel;
everything else comes from the shared package, so an entity cannot exist on one
board and be missing on the other.

## Porting to another ESP32-S3 board

The UPS side is board-independent: `apc_ups` reads the endpoint and the report
layout from the device, so it needs no per-board changes. What a new board costs
is its own package, and on the Y splitter topology it does not even need a VBUS
path -- the splitter supplies the UPS, so the boost, the IO expander and the
interlock guarding them are all CoreS3-specific.

The StickS3 port is written and builds; it is untested pending hardware. Its one
trap is that the M5PM1 PMIC gates the LCD rail on its GPIO2, so the panel stays
dark with no error until `m5pm1` drives it -- the same shape of problem as the
CoreS3's AXP2101 rails. ESP32 classic boards such as the StickC Plus2 cannot
work at all: they have no USB OTG peripheral.

## Secrets

ESPHome resolves `!secret <key>` against a `secrets.yaml` sitting next to the
config it is building. Copy the template and fill it in:

```sh
cp secrets.yaml.example secrets.yaml
head -c 32 /dev/urandom | base64   # value for api_key
```

`secrets.yaml` is gitignored; `secrets.yaml.example` is tracked and holds
placeholders only. A missing key fails at config validation, not at runtime,
so a bad file is caught before anything reaches the device.

## Building and flashing

```sh
esphome compile cores3-bringup.yaml
tools/cores3.sh upload          # flash over USB
tools/cores3.sh logs            # tail the console
tools/cores3_bootlog.py "$(echo /dev/serial/by-id/usb-Espressif_USB_JTAG*)" 30
```

Use `tools/cores3.sh`, not `esphome ... --device /dev/ttyACM*`: kernel names
renumber between boards and will happily flash the wrong target.

The last command resets the board before reading. The ESP32-S3 console runs
over USB-Serial-JTAG and does not re-attach after flashing, so a plain `logs`
sits on a silent port while the board is running perfectly well.

## Testing what the device cannot confirm

`qx_ups` was written from the protocol, without access to the UPS. A wrong field
order there yields plausible numbers rather than an error, so the parsing is a
standalone module with host-side tests:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -o work/qx_protocol_test \
    tools/qx_protocol_test.cpp components/qx_ups/qx_protocol.cpp
work/qx_protocol_test
```

They pin the field order, the latched fault voltage that looks interchangeable
with the input voltage, and the truncated replies a desynced bridge delivers --
which must fail to parse rather than read as data.

`tools/qx_probe.py` answers the remaining question from the host the UPS is
plugged into: which dialect it speaks. It sends status queries only and refuses
anything that tests the battery or switches the load.

## Component sources

Display (`mipi_spi` model `M5CORE`), touch (`ft63x6`) and `usb_host` are
upstream ESPHome. Only the AXP2101 PMU and AW9523B IO expander come from
[`m5stack/esphome-yaml`](https://github.com/m5stack/esphome-yaml), which is the
actively maintained set — the older standalone `esphome-axp2101` forks are not.

## Upstream issue carried here

`m5stack/esphome-yaml`'s `axp2101` sensor platform generates code for a
`battery_charging` binary sensor but does not declare `AUTO_LOAD =
["binary_sensor"]`, so a config using it without some other binary sensor fails
to build on a missing `binary_sensor.h`. Their own sample YAML hides this by
declaring GPIO binary sensors for the audio interrupts.

The board package therefore declares a `status` binary sensor. Once the
`AUTO_LOAD` is fixed upstream, that entity is still worth keeping, but it
stops being required.

## The UPS as seen over USB

Captured from the real device, enumerated by the CoreS3 running on its internal
battery:

```
Manuf: American Power Conversion
Prod:  Back-UPS BX950MI  FW:295202G -302202G
Serial: 9B2504A10895
idVendor 0x051d  idProduct 0x0002
```

| Property | Value |
|---|---|
| Interfaces | 1 |
| Max power | 100mA, well inside the boost's range |
| Endpoint | 0x81, interrupt IN |
| Max packet | 6 bytes |
| Interval | 10ms |

So the component reads asynchronous status changes from one 6-byte interrupt
endpoint, and fetches the HID report descriptor and the feature reports holding
voltages and thresholds over control transfers on EP0.

ESPHome prints `bInterfaceProtocol` under a label reading `bInterfaceClass`
(`usbh_print_intf_desc()` passes the wrong field), so the logged
`bInterfaceClass 0x0` is not the real class -- this device is HID, class 0x03.

## Hardware notes

Both of these come from the CoreS3 having exactly one USB-C port, shared
between flashing, board power and the UPS.

### Host mode makes the CoreS3 a 5V source

The UPS is a pure 5V sink: it draws VBUS from whatever hosts it. Turning on the
`USB Host 5V` switch enables the SY7088 boost (AW9523B P1_7) and then
`USB_OTG_EN` (P0_5), so the CoreS3 drives 5V *out* of its USB-C port.

**The board cannot be powered through that port while it does so**, and the UPS
cannot make up for it: a UPS USB port is a device port, so it sinks VBUS and
sources none. Power the CoreS3 separately.

- **Bench:** run on the internal LiPo. M5Unified guards exactly this case --
  with no battery and external 5V present, enabling the output cuts the board's
  own supply -- so the battery is what makes bench testing safe.
- **Permanent:** a PD splitter cable feeding the CoreS3 while it hosts the UPS.

The switch persists across reboots (`RESTORE_DEFAULT_OFF`: last state, off when
unknown). On the UPS the board is unreachable, so a reboot or OTA has to bring
VBUS back on its own.

The consequence is that a board which was sourcing VBUS will do so again the
moment it boots. Power it from something other than its own USB-C port before
enabling the switch, or the next reboot cuts its supply with no way in.

### Sourcing VBUS needs three bits, not two

`BOOST_EN` (AW9523B P1_7) and `USB_OTG_EN` (P0_5) alone leave VBUS at 0V.
`BUS_OUT_EN` (P0_1) must be high too: while it is low the boost output is tied
to the BUS *input* path and cannot pull VBUS up. Measured 0V with the first two,
5.007V once the third joined them.

M5Unified's `setUsbOutput()` sets only boost and OTG, because its board init
raises `BUS_OUT_EN` separately -- so porting that one function gives a dead
port. The `USB Host 5V` switch drives all three, and unwinds in reverse.

Verified: six VBUS cycles across two runs, six enumerations, six clean
detaches, no resets. Roughly 1.5s from VBUS to `New device`.

### USB-C peripherals cannot be hosted

The CoreS3 has no CC controller for source role -- it presents `Rd` and simply
applies VBUS. USB-C devices never see `Rp`, so they never enable their data
lines, whatever the voltage on VBUS. A USB-C stick, a tablet and a USB-C power
meter all read as dead while VBUS was fine.

Use a USB-C to USB-A adapter and a USB-A device. The UPS is unaffected: its
USB-B port is reached by a plain A-to-B cable, with no CC anywhere.

### Never source VBUS while external power is connected

Enabling the boost with a charger on the Y-adapter's power leg puts both
supplies on the same net. That produced repeated attach/detach churn that never
occurs on battery. Pick one: the board powers the UPS, or external 5V powers
everything and `USB Host 5V` stays off.

`USB Host 5V` now enforces this itself: turning it on while the PMU reports
incoming VBUS is refused and logged, and the switch reports off. The same
interlock covers the batteryless case below, where the two are the same wire.

### Batteryless is the permanent configuration

The internal lithium cell is removed: a pouch cell unattended for a decade is a
fire risk not worth a few hours of ride-through. The board is therefore powered
entirely from outside, and *where* that power arrives decides whether it can
source VBUS at all.

- Fed through the USB-C port (PD splitter): it cannot source, because that is
  the same net it lives on. The UPS must take VBUS from the splitter, and
  `USB Host 5V` stays off permanently. The interlock enforces it.
- Fed through the M5Bus 5V pins from a base: the USB-C port is free, the PMU
  sees no incoming VBUS, and `USB Host 5V` works normally.

Which one a given Y-splitter provides is a property of the cable, not of the
board. Test it with `USB Host 5V` off: if the UPS enumerates anyway, the cable
supplies the peripheral leg.

### A flat cell plus a restored boost is a boot loop

Asserting the boost on an empty battery collapses the rail, the board resets,
and the restored switch asserts it again -- while sourcing VBUS also blocks
charging, so it never recovers. Observed once. `boost_boot_limit` consecutive
boots that never reach `boost_settle_time` abandon the restore.

### The AXP2101 cannot measure VBUS the board sources

Status bit 0x00.5 and the VBUS ADC only see power arriving *from* the port.
They read absent and full-scale-invalid while a meter showed 5.007V, then
correctly reported `vbus_good=1, 4966mV` once an external supply was attached.
Useful for detecting incoming power, useless for verifying our own output.

### Host mode costs the serial console

The ESP32-S3 routes USB-OTG and USB-Serial-JTAG through one PHY on GPIO19/20.
Once `usb_host` claims it, USB flashing and USB logging are gone — updates are
OTA, with BOOT+RESET download mode as the recovery path.

## Licence

Copyright (C) 2026 Holger Steinhaus. Released under the **GNU General Public
Licence, version 2** -- see `LICENSE`. Source files carry
`SPDX-License-Identifier: GPL-2.0-only`.

One exception: `components/apc_ups/hid_pdc.{c,h}` are vendored unmodified from
[hms-homelab/hms-esp-apc](https://github.com/hms-homelab/hms-esp-apc) under the
MIT licence (`components/apc_ups/hid_pdc.LICENSE`), which GPLv2 permits. They
deliberately carry no SPDX marker, because they are kept byte-identical so
upstream fixes can be applied by copying over them -- see `VENDOR.md`.

Credentials are referenced through `!secret` and have never been committed;
see the section above before building.
