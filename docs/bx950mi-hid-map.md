# BX950MI HID field table

Captured from the target UPS (serial 9B2504A10895, FW 295202G -302202G) by
`apc_ups` at enumeration: a 753-byte report descriptor yielding 82 fields, not
truncated, `has_power_page=1`, `uses_report_ids=1`.

Kept here because the UPS runs production equipment and cannot be taken offline
to re-capture. The component still parses the descriptor at runtime -- this is
reference, not a substitute.

## What it tells us

**The interrupt endpoint is 6 bytes wide.**

```
bEndpointAddress 0x81   EP 1 IN
bmAttributes 0x3        INT
wMaxPacketSize 6
bInterval 10
```

ESP-IDF requires every IN transfer to be an integer multiple of the endpoint's
`wMaxPacketSize`. The component first submitted a hardcoded 16 bytes, which is
not a multiple of 6, so the host stack refused all of them and the endpoint
never delivered -- indistinguishable from a UPS that simply never pushes, and
visible only as one warning at connect. Address and length now come from the
configuration descriptor. `bInterval 10` is the device's offer: once the
endpoint works, a status change arrives within about 10 ms.

**Status pushes itself.** Report `0x16` carries the whole `PresentStatus`
bitfield and is an *input* report, so it arrives unprompted on endpoint 0x81
along with `0x0C` RemainingCapacity, `0x13` ACPresent and `0x14`
BelowRemainingCapacityLimit. No polling is needed for state changes.

**Measurements must be polled** over EP0 as feature reports: `0x0F`
RunTimeToEmpty, `0x31` Input.Voltage, `0x26` Battery.Voltage, `0x50`
PercentLoad, `0x32`/`0x33` transfer thresholds, `0x52` ConfigActivePower.

**Scaling comes from the descriptor.** HID expresses voltage in units of
1e-7 V, so `Input.Voltage exp=7` yields volts (230 -> 230V) and
`Battery.Voltage exp=5` yields centivolts (1260 -> 12.60V). `hid_pdc_scale()`
applies the unit exponent and the dimensional correction together.

Fields under `FF86:*` and `FFFF:*` are APC vendor-specific and unnamed; report
`0xE3` is a 7-byte vendor blob available as both input and feature.

## Table

```
[  0] rpt=0x01 feature bit=  0+8  lmin=0 lmax=255 exp=0  0000:0000.0000:0024.0000:00FE
[  1] rpt=0x02 feature bit=  0+8  lmin=0 lmax=255 exp=0  0000:0000.0000:0024.0000:00FF
[  2] rpt=0x03 feature bit=  0+8  lmin=0 lmax=255 exp=0  0000:0000.0000:0024.iDeviceChemistry
[  3] rpt=0x04 feature bit=  0+8  lmin=0 lmax=255 exp=0  0000:0000.0000:0024.iOEMInformation
[  4] rpt=0x05 feature bit=  0+8  lmin=0 lmax=255 exp=0  0000:0000.0000:0024.Rechargeable
[  5] rpt=0x0A feature bit=  0+8  lmin=0 lmax=255 exp=0  0000:0000.0000:0024.iManufacturer
[  6] rpt=0x0C feature bit=  0+8  lmin=0 lmax=255 exp=0  0000:0000.0000:0024.RemainingCapacity
[  7] rpt=0x0C input   bit=  0+8  lmin=0 lmax=255 exp=0  0000:0000.0000:0024.RemainingCapacity
[  8] rpt=0x0D feature bit=  0+8  lmin=0 lmax=100 exp=0  0000:0000.0000:0024.DesignCapacity
[  9] rpt=0x0E feature bit=  0+8  lmin=0 lmax=100 exp=0  0000:0000.0000:0024.FullChargeCapacity
[ 10] rpt=0x0F feature bit=  0+16 lmin=0 lmax=65535 exp=0  0000:0000.0000:0024.RunTimeToEmpty
[ 11] rpt=0x11 feature bit=  0+8  lmin=1 lmax=65535 exp=0  0000:0000.0000:0024.RemainingCapacityLimit
[ 12] rpt=0x13 input   bit=  0+8  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.ACPresent
[ 13] rpt=0x13 feature bit=  0+8  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.ACPresent
[ 14] rpt=0x14 input   bit=  0+8  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.BelowRemainingCapacityLimit
[ 15] rpt=0x14 feature bit=  0+8  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.BelowRemainingCapacityLimit
[ 16] rpt=0x15 feature bit=  0+16 lmin=-1 lmax=32767 exp=0  0000:0000.0000:0024.DelayBeforeShutdown
[ 17] rpt=0x17 feature bit=  0+16 lmin=-1 lmax=65535 exp=0  0000:0000.0000:0024.RemainingTimeLimit
[ 18] rpt=0x18 feature bit=  0+8  lmin=1 lmax=3 exp=0  0000:0000.0000:0024.AudibleAlarmControl
[ 19] rpt=0x16 input   bit=  0+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.Charging
[ 20] rpt=0x16 feature bit=  0+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.Charging
[ 21] rpt=0x16 input   bit=  1+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.Discharging
[ 22] rpt=0x16 feature bit=  1+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.Discharging
[ 23] rpt=0x16 input   bit=  2+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.ACPresent
[ 24] rpt=0x16 feature bit=  2+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.ACPresent
[ 25] rpt=0x16 input   bit=  3+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.BatteryPresent
[ 26] rpt=0x16 feature bit=  3+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.BatteryPresent
[ 27] rpt=0x16 input   bit=  4+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.ShutdownImminent
[ 28] rpt=0x16 feature bit=  4+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.ShutdownImminent
[ 29] rpt=0x16 input   bit=  5+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.RemainingTimeLimitExpired
[ 30] rpt=0x16 feature bit=  5+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.RemainingTimeLimitExpired
[ 31] rpt=0x16 input   bit=  6+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.NeedReplacement
[ 32] rpt=0x16 feature bit=  6+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.NeedReplacement
[ 33] rpt=0x16 input   bit=  7+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.Overload
[ 34] rpt=0x16 feature bit=  7+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.Overload
[ 35] rpt=0x16 input   bit=  8+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.VoltageNotRegulated
[ 36] rpt=0x16 feature bit=  8+1  lmin=0 lmax=1 exp=0  0000:0000.0000:0024.PresentStatus.VoltageNotRegulated
[ 37] rpt=0x20 input   bit=  0+16 lmin=0 lmax=65535 exp=0  0000:0000.Battery.ManufacturerDate
[ 38] rpt=0x20 feature bit=  0+16 lmin=0 lmax=65535 exp=0  0000:0000.Battery.ManufacturerDate
[ 39] rpt=0x21 input   bit=  0+8  lmin=0 lmax=6 exp=0  0000:0000.Battery.Test
[ 40] rpt=0x21 feature bit=  0+8  lmin=0 lmax=6 exp=0  0000:0000.Battery.Test
[ 41] rpt=0x22 feature bit=  0+8  lmin=0 lmax=255 exp=0  0000:0000.Battery.RemainingCapacity
[ 42] rpt=0x23 feature bit=  0+16 lmin=0 lmax=65535 exp=0  0000:0000.Battery.RunTimeToEmpty
[ 43] rpt=0x24 feature bit=  0+16 lmin=0 lmax=65535 exp=0  0000:0000.Battery.RemainingTimeLimit
[ 44] rpt=0x25 feature bit=  0+16 lmin=0 lmax=65535 exp=5  0000:0000.Battery.ConfigVoltage
[ 45] rpt=0x26 feature bit=  0+16 lmin=0 lmax=65535 exp=5  0000:0000.Battery.Voltage
[ 46] rpt=0x30 feature bit=  0+8  lmin=0 lmax=255 exp=7  0000:0000.Input.ConfigVoltage
[ 47] rpt=0x31 feature bit=  0+16 lmin=0 lmax=255 exp=7  0000:0000.Input.Voltage
[ 48] rpt=0x32 feature bit=  0+16 lmin=140 lmax=150 exp=7  0000:0000.Input.LowVoltageTransfer
[ 49] rpt=0x33 feature bit=  0+16 lmin=290 lmax=300 exp=7  0000:0000.Input.HighVoltageTransfer
[ 50] rpt=0x35 feature bit=  0+8  lmin=0 lmax=2 exp=7  0000:0000.Input.FF86:0061
[ 51] rpt=0x36 feature bit=  0+8  lmin=0 lmax=13 exp=7  0000:0000.Input.FF86:0052
[ 52] rpt=0x40 feature bit=  0+8  lmin=0 lmax=255 exp=7  0000:0000.FF86:0005.FF86:007C
[ 53] rpt=0x50 feature bit=  0+8  lmin=0 lmax=100 exp=0  0000:0000.PowerConverter.PercentLoad
[ 54] rpt=0x52 feature bit=  0+16 lmin=0 lmax=65535 exp=7  0000:0000.PowerConverter.ConfigActivePower
[ 55] rpt=0x7F feature bit=  0+8  lmin=0 lmax=255 exp=0  0000:0000.iProduct
[ 56] rpt=0x7E feature bit=  0+8  lmin=0 lmax=255 exp=0  0000:0000.FF86:0042
[ 57] rpt=0x7D feature bit=  0+8  lmin=0 lmax=255 exp=0  0000:0000.iSerialNumber
[ 58] rpt=0x7C feature bit=  0+8  lmin=0 lmax=255 exp=0  0000:0000.iManufacturer
[ 59] rpt=0x7B feature bit=  0+16 lmin=0 lmax=65535 exp=0  0000:0000.ManufacturerDate
[ 60] rpt=0x7A feature bit=  0+1  lmin=0 lmax=1 exp=0  0000:0000.PresentStatus.Charging
[ 61] rpt=0x7A feature bit=  1+1  lmin=0 lmax=1 exp=0  0000:0000.PresentStatus.Discharging
[ 62] rpt=0x7A feature bit=  2+1  lmin=0 lmax=1 exp=0  0000:0000.PresentStatus.ACPresent
[ 63] rpt=0x7A feature bit=  3+1  lmin=0 lmax=1 exp=0  0000:0000.PresentStatus.BatteryPresent
[ 64] rpt=0x7A feature bit=  4+1  lmin=0 lmax=1 exp=0  0000:0000.PresentStatus.NeedReplacement
[ 65] rpt=0x7A feature bit=  5+1  lmin=0 lmax=1 exp=0  0000:0000.PresentStatus.VoltageNotRegulated
[ 66] rpt=0x7A feature bit=  6+1  lmin=0 lmax=1 exp=0  0000:0000.PresentStatus.Overload
[ 67] rpt=0x78 feature bit=  0+8  lmin=1 lmax=3 exp=0  0000:0000.AudibleAlarmControl
[ 68] rpt=0xE3 input   bit=  0+8  lmin=0 lmax=255 exp=0  0000:0000.FFFF:00FF.FFFF:00EF
[ 69] rpt=0xE3 input   bit=  8+8  lmin=0 lmax=255 exp=0  0000:0000.FFFF:00FF.FFFF:00EF
[ 70] rpt=0xE3 input   bit= 16+8  lmin=0 lmax=255 exp=0  0000:0000.FFFF:00FF.FFFF:00EF
[ 71] rpt=0xE3 input   bit= 24+8  lmin=0 lmax=255 exp=0  0000:0000.FFFF:00FF.FFFF:00EF
[ 72] rpt=0xE3 input   bit= 32+8  lmin=0 lmax=255 exp=0  0000:0000.FFFF:00FF.FFFF:00EF
[ 73] rpt=0xE3 input   bit= 40+8  lmin=0 lmax=255 exp=0  0000:0000.FFFF:00FF.FFFF:00EF
[ 74] rpt=0xE3 input   bit= 48+8  lmin=0 lmax=255 exp=0  0000:0000.FFFF:00FF.FFFF:00EF
[ 75] rpt=0xE3 feature bit=  0+8  lmin=0 lmax=255 exp=0  0000:0000.FFFF:00FF.FFFF:00EF
[ 76] rpt=0xE3 feature bit=  8+8  lmin=0 lmax=255 exp=0  0000:0000.FFFF:00FF.FFFF:00EF
[ 77] rpt=0xE3 feature bit= 16+8  lmin=0 lmax=255 exp=0  0000:0000.FFFF:00FF.FFFF:00EF
[ 78] rpt=0xE3 feature bit= 24+8  lmin=0 lmax=255 exp=0  0000:0000.FFFF:00FF.FFFF:00EF
[ 79] rpt=0xE3 feature bit= 32+8  lmin=0 lmax=255 exp=0  0000:0000.FFFF:00FF.FFFF:00EF
[ 80] rpt=0xE3 feature bit= 40+8  lmin=0 lmax=255 exp=0  0000:0000.FFFF:00FF.FFFF:00EF
[ 81] rpt=0xE3 feature bit= 48+8  lmin=0 lmax=255 exp=0  0000:0000.FFFF:00FF.FFFF:00EF
```

## Power sensor accuracy, measured

`UPS Power` is derived as `PercentLoad / 100 x ConfigActivePower`, with no
calibration: the UPS reports its own rating. Validated against an external
meter on the output side, 2026-09-17:

| | |
|---|---|
| Nominal (`ConfigActivePower`, report 0x52) | 520 W, matching the BX950MI rating |
| Baseline load | 16 %, 83 W |
| With test load | 92-93 %, 478.4-483.6 W |
| Reported delta | 395-401 W |
| **Externally measured load** | **399 W** |

The delta brackets the measured value within ~1 W of the mean, so the UPS's own
load estimate needs no correction factor.

The two plateau readings differ by exactly 5.2 W, which is 1 % of 520 W:
`PercentLoad` is whole-percent, so that is the sensor's resolution floor. A few
watts of disagreement is quantisation, not error.

`UPS Overload` correctly stayed off at 93 %.

Had `ConfigActivePower` been read from usage 0x40 (`ConfigVoltage`, ~230)
instead of 0x44, the same load would have computed ~212 W -- wrong, but
plausible enough to accept. The usage IDs are worth checking against
`hid_pdc.c`'s table rather than recalling them.
