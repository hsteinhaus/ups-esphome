# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_BATTERY_CHARGING,
    DEVICE_CLASS_PLUG,
    DEVICE_CLASS_PROBLEM,
)

from . import BASE_SCHEMA, CONF_APC_UPS_ID, Flag

DEPENDENCIES = ["apc_ups"]

CONF_ONLINE = "online"
CONF_CHARGING = "charging"
CONF_DISCHARGING = "discharging"
CONF_LOW_BATTERY = "low_battery"
CONF_REPLACE_BATTERY = "replace_battery"
CONF_OVERLOAD = "overload"
CONF_SHUTDOWN_IMMINENT = "shutdown_imminent"
CONF_BATTERY_PRESENT = "battery_present"

# Keys map to the Flag enum; the C++ side holds the usage paths. All of these
# live in the PresentStatus bitfield, which the UPS pushes on the interrupt
# endpoint, so they update without polling.
FLAGS = {
    CONF_ONLINE: (
        Flag.FLAG_ONLINE,
        binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PLUG),
    ),
    CONF_CHARGING: (
        Flag.FLAG_CHARGING,
        binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_BATTERY_CHARGING
        ),
    ),
    CONF_DISCHARGING: (
        Flag.FLAG_DISCHARGING,
        binary_sensor.binary_sensor_schema(icon="mdi:battery-arrow-down"),
    ),
    CONF_LOW_BATTERY: (
        Flag.FLAG_LOW_BATTERY,
        binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_BATTERY),
    ),
    CONF_REPLACE_BATTERY: (
        Flag.FLAG_REPLACE_BATTERY,
        binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PROBLEM),
    ),
    CONF_OVERLOAD: (
        Flag.FLAG_OVERLOAD,
        binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PROBLEM),
    ),
    CONF_SHUTDOWN_IMMINENT: (
        Flag.FLAG_SHUTDOWN_IMMINENT,
        binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PROBLEM),
    ),
    CONF_BATTERY_PRESENT: (
        Flag.FLAG_BATTERY_PRESENT,
        binary_sensor.binary_sensor_schema(icon="mdi:battery"),
    ),
}

CONFIG_SCHEMA = BASE_SCHEMA.extend(
    {cv.Optional(key): schema for key, (_, schema) in FLAGS.items()}
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_APC_UPS_ID])
    for key, (flag, _) in FLAGS.items():
        if key in config:
            sens = await binary_sensor.new_binary_sensor(config[key])
            cg.add(parent.set_flag_sensor(flag, sens))
