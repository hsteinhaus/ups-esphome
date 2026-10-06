import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv

from . import BASE_SCHEMA, CONF_QX_UPS_ID, Text

DEPENDENCIES = ["qx_ups"]

CONF_MANUFACTURER = "manufacturer"
CONF_MODEL = "model"
CONF_FIRMWARE = "firmware"

# The three fields of the `I` reply. The USB id names the bridge chip, so this
# is the only place the UPS itself says what it is.
TEXTS = {
    CONF_MANUFACTURER: (
        Text.TEXT_MANUFACTURER,
        text_sensor.text_sensor_schema(
            icon="mdi:factory", entity_category="diagnostic"
        ),
    ),
    CONF_MODEL: (
        Text.TEXT_MODEL,
        text_sensor.text_sensor_schema(
            icon="mdi:information-outline", entity_category="diagnostic"
        ),
    ),
    CONF_FIRMWARE: (
        Text.TEXT_FIRMWARE,
        text_sensor.text_sensor_schema(icon="mdi:chip", entity_category="diagnostic"),
    ),
}

CONFIG_SCHEMA = BASE_SCHEMA.extend(
    {cv.Optional(key): schema for key, (_, schema) in TEXTS.items()}
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_QX_UPS_ID])
    for key, (text, _) in TEXTS.items():
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(parent.set_text_sensor(text, sens))
