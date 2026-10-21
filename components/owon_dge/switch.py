# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv

from . import BASE_SCHEMA, CONF_OWON_DGE_ID, Setting, owon_dge_ns

DEPENDENCIES = ["owon_dge"]

OwonDGESwitch = owon_dge_ns.class_("OwonDGESwitch", switch.Switch, cg.Parented)

SWITCHES = {
    "output": (Setting.SETTING_OUTPUT, "mdi:power"),
    # On is High-Z, off is 50 ohm; other loads are the number's.
    "load_high_z": (Setting.SETTING_LOAD, "mdi:omega"),
    "sweep": (Setting.SETTING_SWEEP_STATE, "mdi:chart-bell-curve"),
    "burst": (Setting.SETTING_BURST_STATE, "mdi:pulse"),
}

# DISABLED: a reboot must never change the generator. States come from read-backs.
CONFIG_SCHEMA = BASE_SCHEMA.extend(
    {
        cv.Optional(key): switch.switch_schema(
            OwonDGESwitch, icon=icon, default_restore_mode="DISABLED"
        )
        for key, (_, icon) in SWITCHES.items()
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_OWON_DGE_ID])
    for key, (setting, _) in SWITCHES.items():
        if key in config:
            sw = await switch.new_switch(config[key], setting)
            await cg.register_parented(sw, parent)
            cg.add(parent.set_switch(setting, sw))
