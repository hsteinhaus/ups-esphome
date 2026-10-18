# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv

from . import BASE_SCHEMA, CONF_OWON_PSU_ID, owon_psu_ns

DEPENDENCIES = ["owon_psu"]

OwonPSUSwitch = owon_psu_ns.class_("OwonPSUSwitch", switch.Switch, cg.Parented)

CONF_OUTPUT = "output"

# DISABLED: a reboot must never switch the output. The state comes from OUTP?.
CONFIG_SCHEMA = BASE_SCHEMA.extend(
    {
        cv.Optional(CONF_OUTPUT): switch.switch_schema(
            OwonPSUSwitch, icon="mdi:power", default_restore_mode="DISABLED"
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_OWON_PSU_ID])
    if CONF_OUTPUT in config:
        sw = await switch.new_switch(config[CONF_OUTPUT])
        await cg.register_parented(sw, parent)
        cg.add(parent.set_output_switch(sw))
