# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import (
    CONF_MAX_VALUE,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_VOLTAGE,
    UNIT_AMPERE,
    UNIT_VOLT,
)

from . import BASE_SCHEMA, CONF_OWON_PSU_ID, Setting, owon_psu_ns

DEPENDENCIES = ["owon_psu"]

OwonPSUNumber = owon_psu_ns.class_("OwonPSUNumber", number.Number, cg.Parented)

# Upper bounds are the SPE6103's; max_value narrows the slider for a smaller
# model. The PSU still refuses anything its own limits forbid.
NUMBERS = {
    "voltage": (Setting.SETTING_VOLTAGE, UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 60.0),
    "current": (Setting.SETTING_CURRENT, UNIT_AMPERE, DEVICE_CLASS_CURRENT, 10.0),
    "voltage_limit": (Setting.SETTING_VOLTAGE_LIMIT, UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 62.0),
    "current_limit": (Setting.SETTING_CURRENT_LIMIT, UNIT_AMPERE, DEVICE_CLASS_CURRENT, 10.2),
}

CONFIG_SCHEMA = BASE_SCHEMA.extend(
    {
        cv.Optional(key): number.number_schema(
            OwonPSUNumber, unit_of_measurement=unit, device_class=device_class
        ).extend({cv.Optional(CONF_MAX_VALUE, default=top): cv.positive_float})
        for key, (_, unit, device_class, top) in NUMBERS.items()
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_OWON_PSU_ID])
    for key, (setting, _, _, _) in NUMBERS.items():
        if key in config:
            conf = config[key]
            num = await number.new_number(
                conf, setting, min_value=0.0, max_value=conf[CONF_MAX_VALUE], step=0.01
            )
            await cg.register_parented(num, parent)
            cg.add(parent.set_number(setting, num))
