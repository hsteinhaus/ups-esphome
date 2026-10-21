# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import (
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_STEP,
    UNIT_DEGREES,
    UNIT_HERTZ,
    UNIT_OHM,
    UNIT_PERCENT,
    UNIT_SECOND,
    UNIT_VOLT,
)

from . import BASE_SCHEMA, CONF_OWON_DGE_ID, Setting, owon_dge_ns

DEPENDENCIES = ["owon_dge"]

OwonDGENumber = owon_dge_ns.class_("OwonDGENumber", number.Number, cg.Parented)

UNIT_VPP = "Vpp"
UNIT_MICROSECOND = "µs"

# Bounds are the DGE1060's as far as checked on the unit; the generator still
# refuses what its own limits forbid, and the entity then shows its read-back.
# ESPHome shows a number to its step, so no step is finer than a milli-unit;
# pulse timing is in microseconds for that reason.
NUMBERS = {
    "load": (Setting.SETTING_LOAD, UNIT_OHM, 1, 10000, 1),
    "frequency": (Setting.SETTING_FREQUENCY, UNIT_HERTZ, 0.001, 60e6, 0.001),
    "amplitude": (Setting.SETTING_AMPLITUDE, UNIT_VPP, 0.001, 20, 0.001),
    "offset": (Setting.SETTING_OFFSET, UNIT_VOLT, -10, 10, 0.001),
    "phase": (Setting.SETTING_PHASE, UNIT_DEGREES, 0, 360, 0.1),
    "ramp_symmetry": (Setting.SETTING_RAMP_SYMMETRY, UNIT_PERCENT, 0, 100, 0.1),
    "pulse_duty": (Setting.SETTING_PULSE_DUTY, UNIT_PERCENT, 0, 100, 0.1),
    "pulse_width": (Setting.SETTING_PULSE_WIDTH, UNIT_MICROSECOND, 0.01, 1e9, 0.001),
    "pulse_lead": (Setting.SETTING_PULSE_LEAD, UNIT_MICROSECOND, 0.01, 1e9, 0.001),
    "pulse_trail": (Setting.SETTING_PULSE_TRAIL, UNIT_MICROSECOND, 0.01, 1e9, 0.001),
    "am_depth": (Setting.SETTING_AM_DEPTH, UNIT_PERCENT, 0, 100, 0.1),
    "am_frequency": (Setting.SETTING_AM_FREQUENCY, UNIT_HERTZ, 2e-3, 1e5, 1e-3),
    "fm_deviation": (Setting.SETTING_FM_DEVIATION, UNIT_HERTZ, 0, 30e6, 1e-3),
    "fm_frequency": (Setting.SETTING_FM_FREQUENCY, UNIT_HERTZ, 2e-3, 1e5, 1e-3),
    "fsk_frequency": (Setting.SETTING_FSK_FREQUENCY, UNIT_HERTZ, 0.001, 60e6, 0.001),
    "fsk_rate": (Setting.SETTING_FSK_RATE, UNIT_HERTZ, 2e-3, 1e5, 1e-3),
    "sweep_time": (Setting.SETTING_SWEEP_TIME, UNIT_SECOND, 1e-3, 500, 1e-3),
    "sweep_start": (Setting.SETTING_SWEEP_START, UNIT_HERTZ, 0.001, 60e6, 0.001),
    "sweep_stop": (Setting.SETTING_SWEEP_STOP, UNIT_HERTZ, 0.001, 60e6, 0.001),
    "burst_cycles": (Setting.SETTING_BURST_CYCLES, None, 1, 500000, 1),
    "burst_period": (Setting.SETTING_BURST_PERIOD, UNIT_SECOND, 0.001, 500, 0.001),
}


def _schema(unit, lo, hi, step):
    extra = {"unit_of_measurement": unit} if unit else {}
    return number.number_schema(OwonDGENumber, **extra).extend(
        {
            cv.Optional(CONF_MIN_VALUE, default=lo): cv.float_,
            cv.Optional(CONF_MAX_VALUE, default=hi): cv.float_,
            cv.Optional(CONF_STEP, default=step): cv.positive_float,
        }
    )


CONFIG_SCHEMA = BASE_SCHEMA.extend(
    {cv.Optional(key): _schema(*rest) for key, (_, *rest) in NUMBERS.items()}
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_OWON_DGE_ID])
    for key, (setting, *_) in NUMBERS.items():
        if key in config:
            conf = config[key]
            num = await number.new_number(
                conf,
                setting,
                min_value=conf[CONF_MIN_VALUE],
                max_value=conf[CONF_MAX_VALUE],
                step=conf[CONF_STEP],
            )
            await cg.register_parented(num, parent)
            cg.add(parent.set_number(setting, num))
