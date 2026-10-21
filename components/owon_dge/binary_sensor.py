# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_CONNECTIVITY

from . import BASE_SCHEMA, CONF_OWON_DGE_ID

DEPENDENCIES = ["owon_dge"]

CONF_CONNECTED = "connected"

CONFIG_SCHEMA = BASE_SCHEMA.extend(
    {
        cv.Optional(CONF_CONNECTED): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY, entity_category="diagnostic"
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_OWON_DGE_ID])
    if CONF_CONNECTED in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_CONNECTED])
        cg.add(parent.set_connected_sensor(sens))
