# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import i2c
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@hsteinhaus"]
DEPENDENCIES = ["i2c"]

CONF_LCD_POWER = "lcd_power"

m5pm1_ns = cg.esphome_ns.namespace("m5pm1")
M5PM1 = m5pm1_ns.class_("M5PM1", cg.Component, i2c.I2CDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(M5PM1),
            cv.Optional(CONF_LCD_POWER, default=True): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x6E))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_lcd_power(config[CONF_LCD_POWER]))
