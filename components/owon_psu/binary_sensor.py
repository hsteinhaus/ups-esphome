# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_CONNECTIVITY, DEVICE_CLASS_PROBLEM

from . import BASE_SCHEMA, CONF_OWON_PSU_ID, Flag

DEPENDENCIES = ["owon_psu"]

# The protection fields of MEAS:ALL:INFO?. Their order is inferred, not
# observed -- see owon_psu.h.
FLAGS = {
    "ovp": (Flag.FLAG_OVP, binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PROBLEM)),
    "ocp": (Flag.FLAG_OCP, binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PROBLEM)),
    "otp": (Flag.FLAG_OTP, binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PROBLEM)),
    "connected": (
        Flag.FLAG_CONNECTED,
        binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY, entity_category="diagnostic"
        ),
    ),
}

CONFIG_SCHEMA = BASE_SCHEMA.extend(
    {cv.Optional(key): schema for key, (_, schema) in FLAGS.items()}
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_OWON_PSU_ID])
    for key, (flag, _) in FLAGS.items():
        if key in config:
            sens = await binary_sensor.new_binary_sensor(config[key])
            cg.add(parent.set_flag_sensor(flag, sens))
