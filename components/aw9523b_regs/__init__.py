# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import i2c
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["i2c"]
CODEOWNERS = ["@hsteinhaus"]

aw9523b_regs_ns = cg.esphome_ns.namespace("aw9523b_regs")
AW9523BRegs = aw9523b_regs_ns.class_("AW9523BRegs", cg.PollingComponent, i2c.I2CDevice)

CONFIG_SCHEMA = (
    cv.Schema({cv.GenerateID(): cv.declare_id(AW9523BRegs)})
    .extend(cv.polling_component_schema("5s"))
    .extend(i2c.i2c_device_schema(0x58))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
