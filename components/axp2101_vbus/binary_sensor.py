# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import binary_sensor, i2c
import esphome.config_validation as cv

from . import axp2101_vbus_ns

CONF_PMU_INIT = "pmu_init"

DEPENDENCIES = ["i2c"]

AXP2101Vbus = axp2101_vbus_ns.class_(
    "AXP2101Vbus", binary_sensor.BinarySensor, cg.PollingComponent, i2c.I2CDevice
)

CONFIG_SCHEMA = (
    binary_sensor.binary_sensor_schema(AXP2101Vbus)
    .extend(cv.polling_component_schema("5s"))
    .extend(i2c.i2c_device_schema(0x34))
    .extend({cv.Optional(CONF_PMU_INIT, default=True): cv.boolean})
)


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_pmu_init(config[CONF_PMU_INIT]))
