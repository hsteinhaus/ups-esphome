#!/usr/bin/env bash
# Flash or tail the CoreS3, addressing it only by its stable by-id path.
# Kernel names (/dev/ttyACM*) renumber across replugs and would target
# whichever board happens to hold that number.
#
# Usage: tools/cores3.sh {upload|logs|run} [config.yaml]
set -euo pipefail

DEVICE_ID="usb-Espressif_USB_JTAG_serial_debug_unit_24:58:7C:E9:32:48-if00"
DEVICE="/dev/serial/by-id/${DEVICE_ID}"

if [[ $# -lt 1 ]]; then
    echo "usage: $0 {upload|logs|run} [config.yaml]" >&2
    exit 2
fi
action="$1"
config="${2:-cores3-bringup.yaml}"

if [[ ! -e $DEVICE ]]; then
    echo "CoreS3 not present at $DEVICE" >&2
    echo "Attached instead:" >&2
    ls /dev/serial/by-id/ >&2 || echo "  (none)" >&2
    exit 1
fi

esphome="$(dirname "$0")/../work/venv/bin/esphome"
[[ -x $esphome ]] || esphome=esphome

exec "$esphome" "$action" "$config" --device "$DEVICE"
