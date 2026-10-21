# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv

from . import BASE_SCHEMA, CONF_OWON_DGE_ID, Setting, owon_dge_ns

DEPENDENCIES = ["owon_dge"]

OwonDGESelect = owon_dge_ns.class_("OwonDGESelect", select.Select, cg.Parented)

# Options in the order of owon_dge_protocol.cpp's token lists: the index is
# what goes on the wire.
MOD_SHAPES = ["Sine", "Square", "Ramp", "Noise"]
SOURCES = ["Internal", "External"]
TRIGGERS = ["Timer", "Manual", "External"]
SELECTS = {
    "shape": (Setting.SETTING_SHAPE, ["Sine", "Square", "Pulse", "Ramp", "Noise", "DC"]),
    "am_source": (Setting.SETTING_AM_SOURCE, SOURCES),
    "am_shape": (Setting.SETTING_AM_SHAPE, MOD_SHAPES),
    "fm_source": (Setting.SETTING_FM_SOURCE, SOURCES),
    "fm_shape": (Setting.SETTING_FM_SHAPE, MOD_SHAPES),
    "fsk_source": (Setting.SETTING_FSK_SOURCE, SOURCES),
    "sweep_spacing": (Setting.SETTING_SWEEP_SPACING, ["Linear", "Log"]),
    "sweep_source": (Setting.SETTING_SWEEP_SOURCE, TRIGGERS),
    "burst_mode": (Setting.SETTING_BURST_MODE, ["Triggered", "Gated"]),
    "burst_source": (Setting.SETTING_BURST_SOURCE, TRIGGERS),
    "burst_gate_polarity": (Setting.SETTING_BURST_GATE_POLARITY, ["Positive", "Negative"]),
    # Not a setting: read from and written to the per-type STATe ones.
    "modulation": (Setting.SETTING_COUNT, ["Off", "AM", "FM", "FSK"]),
}

CONFIG_SCHEMA = BASE_SCHEMA.extend(
    {cv.Optional(key): select.select_schema(OwonDGESelect) for key in SELECTS}
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_OWON_DGE_ID])
    for key, (setting, options) in SELECTS.items():
        if key in config:
            sel = await select.new_select(config[key], setting, options=options)
            await cg.register_parented(sel, parent)
            if key == "modulation":
                cg.add(parent.set_modulation_select(sel))
            else:
                cg.add(parent.set_select(setting, sel))
