# apc-esphome

ESPHome component to read an **APC Back-UPS BX950MI** (USB HID Power Device,
`051d:0002`) from an **M5Stack CoreS3**, with status on the built-in display.

Stage 1 — in this repo now — is hardware bring-up. The UPS component follows.

## Layout

| Path | Purpose |
|---|---|
| `boards/m5stack-cores3.yaml` | Board package: PMU rails, IO expander, display, touch, USB host power |
| `cores3-bringup.yaml` | Stage-1 test config: display, touch and PMU telemetry |

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

The switch defaults to off and never restores on, so a reboot cannot silently
drop the board's power.

### Host mode costs the serial console

The ESP32-S3 routes USB-OTG and USB-Serial-JTAG through one PHY on GPIO19/20.
Once `usb_host` claims it, USB flashing and USB logging are gone — updates are
OTA, with BOOT+RESET download mode as the recovery path.
