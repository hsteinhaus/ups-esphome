# Vendored: hid_pdc

`hid_pdc.c` and `hid_pdc.h` are copied unmodified from
[hms-homelab/hms-esp-apc](https://github.com/hms-homelab/hms-esp-apc)
(`main/hid_pdc.{c,h}`), MIT licensed -- see `hid_pdc.LICENSE`.

They are kept byte-identical so upstream fixes can be re-applied by copying over
them. C linkage is handled by wrapping the include in `apc_ups.h`, not by
editing the header.

## Why this code

It walks the HID report descriptor the device itself provides and builds a
field table keyed by usage path, instead of hardcoding report IDs and bit
offsets. Report IDs are assigned arbitrarily per vendor firmware, so offsets
learned from one UPS are a fact about that UPS alone; upstream's changelog
records the bugs that caused (low battery asserted at 97% charge, beeper
reported as self-test result).

That matters more here than upstream: the target BX950MI runs production
equipment and cannot be taken offline to capture descriptors, so parsing what
the device reports at enumeration is the only ground truth available.

It also records enclosing collections per field, which is required because the
same leaf usage means different things in different places -- `Voltage`
(0x84:0x30) appears under `PowerSummary`, `Input`, `Output` and `BatterySystem`.
NUT carries duplicate mapping rows for the same reason.
