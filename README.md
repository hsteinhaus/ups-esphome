# apc-esphome

ESPHome component to read an **APC Back-UPS BX950MI** (USB HID Power Device,
`051d:0002`) from an **M5Stack CoreS3**, with status on the built-in display.

Stage 1 — in this repo now — is hardware bring-up. The UPS component follows.

## Layout

| Path | Purpose |
|---|---|
| `boards/m5stack-cores3.yaml` | Board package: PMU rails, IO expander, display, touch, USB host power |
| `cores3-bringup.yaml` | Stage-1 test config: display, touch and PMU telemetry |

## Building

Create `secrets.yaml` next to the config with `wifi_ssid`, `wifi_password`,
`api_key` and `ota_password`, then:

```sh
esphome run cores3-bringup.yaml
```

## Component sources

Display (`mipi_spi` model `M5CORE`), touch (`ft63x6`) and `usb_host` are
upstream ESPHome. Only the AXP2101 PMU and AW9523B IO expander come from
[`m5stack/esphome-yaml`](https://github.com/m5stack/esphome-yaml), which is the
actively maintained set — the older standalone `esphome-axp2101` forks are not.

## Hardware notes

Both of these come from the CoreS3 having exactly one USB-C port, shared
between flashing, board power and the UPS.

### Host mode makes the CoreS3 a 5V source

The UPS is a pure 5V sink: it draws VBUS from whatever hosts it. Turning on the
`USB Host 5V` switch enables the SY7088 boost (AW9523B P1_7) and then
`USB_OTG_EN` (P0_5), so the CoreS3 drives 5V *out* of its USB-C port.

**The board cannot be powered through that port while it does so.** Supply it
from the internal LiPo or from 5V on the M-Bus / Grove header first. M5Unified
guards this same case: with no battery and external 5V present, enabling the
output cuts the board's own supply.

The switch defaults to off and never restores on, so a reboot cannot silently
drop the board's power.

### Host mode costs the serial console

The ESP32-S3 routes USB-OTG and USB-Serial-JTAG through one PHY on GPIO19/20.
Once `usb_host` claims it, USB flashing and USB logging are gone — updates are
OTA, with BOOT+RESET download mode as the recovery path.
