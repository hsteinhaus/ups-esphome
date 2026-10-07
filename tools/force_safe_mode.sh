#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Recover an ESPHome board whose running firmware crashes during OTA, and so
# cannot receive the firmware that fixes the crash.
#
# This repo needed it once, on 2026-10-07: the IDF USB host ISR is
# flash-resident with no IRAM option, so a USB transfer completing inside the
# cache-disabled erase window asserts in the DWC driver, and esp_ota_begin
# panics. See docs/STATUS.md, "The USB host ISR cannot survive a flash erase".
#
# Every OTA attempt crashes the board inside safe_mode's boot_is_good_after
# window, so the boot-loop counter climbs instead of resetting. After
# num_attempts (10 by default) the board boots into safe mode, where the
# offending component never starts and the OTA lands.
#
# ATTEMPT_TIMEOUT is the whole trick. The upload client keeps waiting long
# after the board has crashed and rebooted, so leaving it to time out on its
# own puts the next attempt more than boot_is_good_after (60s) past the
# reboot -- the counter resets every cycle and safe mode never arrives. Cut
# the client off instead, so the next crash lands inside the window.
#
# Usage: tools/force_safe_mode.sh <config.yaml> <host> [max-attempts]
set -u

CONFIG=${1:?usage: force_safe_mode.sh <config.yaml> <host> [max-attempts]}
HOST=${2:?usage: force_safe_mode.sh <config.yaml> <host> [max-attempts]}
MAX=${3:-16}
ESPHOME=${ESPHOME:-work/venv/bin/esphome}
PORT=3232
ATTEMPT_TIMEOUT=${ATTEMPT_TIMEOUT:-25}

# The board is reachable again once it reopens the OTA port; polling that is
# what keeps each attempt inside the 60s window that increments the counter.
wait_up() {
  for _ in $(seq 1 90); do
    (exec 3<>/dev/tcp/"$HOST"/$PORT) 2>/dev/null && { exec 3<&-; return 0; }
    sleep 1
  done
  return 1
}

for attempt in $(seq 1 "$MAX"); do
  echo "=== attempt $attempt  $(date +%T)"
  if ! wait_up; then
    echo "board never opened $PORT; stopping"
    exit 1
  fi
  out=$(timeout "$ATTEMPT_TIMEOUT" "$ESPHOME" upload "$CONFIG" --device "$HOST" 2>&1)
  echo "$out" | tail -3
  if echo "$out" | grep -q "OTA successful"; then
    echo "=== RECOVERED on attempt $attempt  $(date +%T)"
    exit 0
  fi
done

echo "=== gave up after $MAX attempts"
exit 1
