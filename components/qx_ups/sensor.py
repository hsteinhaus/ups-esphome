# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_POWER_FACTOR,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_HERTZ,
    UNIT_PERCENT,
    UNIT_VOLT,
    UNIT_WATT,
)

from . import BASE_SCHEMA, CONF_QX_UPS_ID, Metric

DEPENDENCIES = ["qx_ups"]

CONF_INPUT_VOLTAGE = "input_voltage"
CONF_INPUT_FAULT_VOLTAGE = "input_fault_voltage"
CONF_OUTPUT_VOLTAGE = "output_voltage"
CONF_LOAD = "load"
CONF_INPUT_FREQUENCY = "input_frequency"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_TEMPERATURE = "temperature"
CONF_POWER = "power"
CONF_BATTERY_LEVEL = "battery_level"
CONF_RATED_VOLTAGE = "rated_voltage"
CONF_RATED_CURRENT = "rated_current"
CONF_RATED_BATTERY_VOLTAGE = "rated_battery_voltage"

# Keys map to the Metric enum; every one but the last two comes straight out
# of the Q1 reply, in the order the device sends them.
METRICS = {
    CONF_INPUT_VOLTAGE: (
        Metric.METRIC_INPUT_VOLTAGE,
        sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=1,
        ),
    ),
    # The voltage at the moment the UPS last switched to battery, latched until
    # the next event -- diagnostic, not a live reading.
    CONF_INPUT_FAULT_VOLTAGE: (
        Metric.METRIC_INPUT_FAULT_VOLTAGE,
        sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            device_class=DEVICE_CLASS_VOLTAGE,
            accuracy_decimals=1,
            entity_category="diagnostic",
        ),
    ),
    CONF_OUTPUT_VOLTAGE: (
        Metric.METRIC_OUTPUT_VOLTAGE,
        sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=1,
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
    CONF_INPUT_FREQUENCY: (
        Metric.METRIC_INPUT_FREQUENCY,
        sensor.sensor_schema(
            unit_of_measurement=UNIT_HERTZ,
            device_class=DEVICE_CLASS_FREQUENCY,
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
    CONF_TEMPERATURE: (
        Metric.METRIC_TEMPERATURE,
        sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=1,
        ),
    ),
    # Derived from load and the nominal_power given in YAML: the protocol
    # reports no watts.
    CONF_POWER: (
        Metric.METRIC_POWER,
        sensor.sensor_schema(
            unit_of_measurement=UNIT_WATT,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=0,
            icon="mdi:flash",
        ),
    ),
    # The `F` reply, asked once. Diagnostic, but on a UPS whose model number
    # says nothing these are the only nameplate available.
    CONF_RATED_VOLTAGE: (
        Metric.METRIC_RATED_VOLTAGE,
        sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            device_class=DEVICE_CLASS_VOLTAGE,
            accuracy_decimals=1,
            entity_category="diagnostic",
        ),
    ),
    CONF_RATED_CURRENT: (
        Metric.METRIC_RATED_CURRENT,
        sensor.sensor_schema(
            unit_of_measurement=UNIT_AMPERE,
            device_class=DEVICE_CLASS_CURRENT,
            accuracy_decimals=1,
            entity_category="diagnostic",
        ),
    ),
    CONF_RATED_BATTERY_VOLTAGE: (
        Metric.METRIC_RATED_BATTERY_VOLTAGE,
        sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            device_class=DEVICE_CLASS_VOLTAGE,
            accuracy_decimals=2,
            entity_category="diagnostic",
        ),
    ),
    # Estimated from battery voltage: the protocol reports no charge.
    CONF_BATTERY_LEVEL: (
        Metric.METRIC_BATTERY_LEVEL,
        sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            device_class=DEVICE_CLASS_BATTERY,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=0,
        ),
    ),
}

CONFIG_SCHEMA = BASE_SCHEMA.extend(
    {cv.Optional(key): schema for key, (_, schema) in METRICS.items()}
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_QX_UPS_ID])
    for key, (metric, _) in METRICS.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(parent.set_metric_sensor(metric, sens))
