# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv

from . import BASE_SCHEMA, CONF_OWON_DGE_ID, Text

DEPENDENCIES = ["owon_dge"]

TEXTS = {
    "model": (Text.TEXT_MODEL, "mdi:information-outline"),
    "serial_number": (Text.TEXT_SERIAL, "mdi:barcode"),
    "firmware": (Text.TEXT_FIRMWARE, "mdi:chip"),
}

CONFIG_SCHEMA = BASE_SCHEMA.extend(
    {
        cv.Optional(key): text_sensor.text_sensor_schema(icon=icon, entity_category="diagnostic")
        for key, (_, icon) in TEXTS.items()
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_OWON_DGE_ID])
    for key, (text, _) in TEXTS.items():
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(parent.set_text_sensor(text, sens))
