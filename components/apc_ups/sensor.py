import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_POWER_FACTOR,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
    UNIT_SECOND,
    UNIT_VOLT,
)

from . import BASE_SCHEMA, CONF_APC_UPS_ID, Metric

DEPENDENCIES = ["apc_ups"]

CONF_BATTERY_LEVEL = "battery_level"
CONF_RUNTIME = "runtime"
CONF_INPUT_VOLTAGE = "input_voltage"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_LOAD = "load"

# Keys map to the Metric enum; the C++ side holds the usage paths.
METRICS = {
    CONF_BATTERY_LEVEL: (
        Metric.METRIC_BATTERY_LEVEL,
        sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            device_class=DEVICE_CLASS_BATTERY,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=0,
        ),
    ),
    CONF_RUNTIME: (
        Metric.METRIC_RUNTIME,
        sensor.sensor_schema(
            unit_of_measurement=UNIT_SECOND,
            device_class=DEVICE_CLASS_DURATION,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=0,
            icon="mdi:timer-outline",
        ),
    ),
    CONF_INPUT_VOLTAGE: (
        Metric.METRIC_INPUT_VOLTAGE,
        sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=1,
        ),
    ),
    CONF_BATTERY_VOLTAGE: (
        Metric.METRIC_BATTERY_VOLTAGE,
        sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=2,
        ),
    ),
    CONF_LOAD: (
        Metric.METRIC_LOAD,
        sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            device_class=DEVICE_CLASS_POWER_FACTOR,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=0,
            icon="mdi:gauge",
        ),
    ),
}

CONFIG_SCHEMA = BASE_SCHEMA.extend(
    {cv.Optional(key): schema for key, (_, schema) in METRICS.items()}
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_APC_UPS_ID])
    for key, (metric, _) in METRICS.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(parent.set_metric_sensor(metric, sens))
