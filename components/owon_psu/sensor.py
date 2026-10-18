# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_VOLT,
    UNIT_WATT,
)

from . import BASE_SCHEMA, CONF_OWON_PSU_ID, Metric

DEPENDENCIES = ["owon_psu"]

# Keys map to the Metric enum, in the order MEAS:ALL:INFO? carries them.
METRICS = {
    "voltage": (Metric.METRIC_VOLTAGE, UNIT_VOLT, DEVICE_CLASS_VOLTAGE),
    "current": (Metric.METRIC_CURRENT, UNIT_AMPERE, DEVICE_CLASS_CURRENT),
    "power": (Metric.METRIC_POWER, UNIT_WATT, DEVICE_CLASS_POWER),
}

CONFIG_SCHEMA = BASE_SCHEMA.extend(
    {
        cv.Optional(key): sensor.sensor_schema(
            unit_of_measurement=unit,
            device_class=device_class,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=3,
        )
        for key, (_, unit, device_class) in METRICS.items()
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_OWON_PSU_ID])
    for key, (metric, _, _) in METRICS.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(parent.set_metric_sensor(metric, sens))
