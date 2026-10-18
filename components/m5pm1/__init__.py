# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import binary_sensor, i2c
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@hsteinhaus"]
DEPENDENCIES = ["i2c"]
AUTO_LOAD = ["binary_sensor"]

CONF_LCD_POWER = "lcd_power"
CONF_OK = "ok"

m5pm1_ns = cg.esphome_ns.namespace("m5pm1")
M5PM1 = m5pm1_ns.class_("M5PM1", cg.Component, i2c.I2CDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(M5PM1),
            cv.Optional(CONF_LCD_POWER, default=True): cv.boolean,
            # Live proof the PMIC answered and the LCD rail came up; the deploy
            # tool reads it, since INFO logging hides the config banner.
            cv.Optional(CONF_OK): binary_sensor.binary_sensor_schema(
                entity_category="diagnostic", icon="mdi:chip"
            ),
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
    if CONF_OK in config:
        cg.add(var.set_ok_sensor(await binary_sensor.new_binary_sensor(config[CONF_OK])))
