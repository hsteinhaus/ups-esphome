# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv

from . import BASE_SCHEMA, CONF_OWON_PSU_ID, Text

DEPENDENCIES = ["owon_psu"]

TEXTS = {
    "model": (Text.TEXT_MODEL, "mdi:information-outline", "diagnostic"),
    "serial_number": (Text.TEXT_SERIAL, "mdi:barcode", "diagnostic"),
    "firmware": (Text.TEXT_FIRMWARE, "mdi:chip", "diagnostic"),
    # CV or CC; see parse_info_ for how sure that is.
    "mode": (Text.TEXT_MODE, "mdi:sine-wave", None),
}

CONFIG_SCHEMA = BASE_SCHEMA.extend(
    {
        cv.Optional(key): text_sensor.text_sensor_schema(
            icon=icon, **({"entity_category": cat} if cat else {})
        )
        for key, (_, icon, cat) in TEXTS.items()
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_OWON_PSU_ID])
    for key, (text, _, _) in TEXTS.items():
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(parent.set_text_sensor(text, sens))
