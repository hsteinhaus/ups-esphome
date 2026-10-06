import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_PLUG,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_RUNNING,
)

from . import BASE_SCHEMA, CONF_QX_UPS_ID, Flag

DEPENDENCIES = ["qx_ups"]

CONF_ONLINE = "online"
CONF_ON_BATTERY = "on_battery"
CONF_LOW_BATTERY = "low_battery"
CONF_BOOST_BUCK = "boost_buck"
CONF_UPS_FAILED = "ups_failed"
CONF_TEST_IN_PROGRESS = "test_in_progress"
CONF_SHUTDOWN_ACTIVE = "shutdown_active"
CONF_BEEPER_ON = "beeper_on"

# Keys map to the Flag enum. All of these come from the eight status bits at
# the end of a Q1 reply, so they are only as fresh as status_interval.
FLAGS = {
    CONF_ONLINE: (
        Flag.FLAG_ONLINE,
        binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PLUG),
    ),
    CONF_ON_BATTERY: (
        Flag.FLAG_ON_BATTERY,
        binary_sensor.binary_sensor_schema(icon="mdi:battery-arrow-down"),
    ),
    CONF_LOW_BATTERY: (
        Flag.FLAG_LOW_BATTERY,
        binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_BATTERY),
    ),
    # Line-interactive units correct a sagging or high mains without going to
    # battery; seeing this tells you the mains is poor, not that it failed.
    CONF_BOOST_BUCK: (
        Flag.FLAG_BOOST_BUCK,
        binary_sensor.binary_sensor_schema(icon="mdi:sine-wave"),
    ),
    CONF_UPS_FAILED: (
        Flag.FLAG_UPS_FAILED,
        binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PROBLEM),
    ),
    CONF_TEST_IN_PROGRESS: (
        Flag.FLAG_TEST_IN_PROGRESS,
        binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_RUNNING, entity_category="diagnostic"
        ),
    ),
    CONF_SHUTDOWN_ACTIVE: (
        Flag.FLAG_SHUTDOWN_ACTIVE,
        binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PROBLEM),
    ),
    CONF_BEEPER_ON: (
        Flag.FLAG_BEEPER_ON,
        binary_sensor.binary_sensor_schema(
            icon="mdi:volume-high", entity_category="diagnostic"
        ),
    ),
}

CONFIG_SCHEMA = BASE_SCHEMA.extend(
    {cv.Optional(key): schema for key, (_, schema) in FLAGS.items()}
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_QX_UPS_ID])
    for key, (flag, _) in FLAGS.items():
        if key in config:
            sens = await binary_sensor.new_binary_sensor(config[key])
            cg.add(parent.set_flag_sensor(flag, sens))
