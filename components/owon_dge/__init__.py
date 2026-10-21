# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import usb_host
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["usb_host"]
AUTO_LOAD = ["binary_sensor", "text_sensor", "number", "select", "switch"]
CODEOWNERS = ["@hsteinhaus"]

CONF_OWON_DGE_ID = "owon_dge_id"
CONF_STATUS_INTERVAL = "status_interval"
CONF_SETTINGS_INTERVAL = "settings_interval"
CONF_REPLY_TIMEOUT = "reply_timeout"

owon_dge_ns = cg.esphome_ns.namespace("owon_dge")
OwonDGE = owon_dge_ns.class_("OwonDGE", usb_host.USBClient)
Setting = owon_dge_ns.enum("Setting")
Text = owon_dge_ns.enum("Text")

BASE_SCHEMA = cv.Schema({cv.GenerateID(CONF_OWON_DGE_ID): cv.use_id(OwonDGE)})

# 5345:1234 is the "PC" USB mode, the front panel's default.
CONFIG_SCHEMA = usb_host.usb_device_schema(OwonDGE, 0x5345, 0x1234).extend(
    {
        # Output, shape, frequency, amplitude, offset and the exclusive mode
        # states; a set is read back immediately regardless.
        cv.Optional(
            CONF_STATUS_INTERVAL, default="1s"
        ): cv.positive_time_period_milliseconds,
        # One round over every setting.
        cv.Optional(
            CONF_SETTINGS_INTERVAL, default="5s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_REPLY_TIMEOUT, default="500ms"
        ): cv.positive_time_period_milliseconds,
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID], config[usb_host.CONF_VID], config[usb_host.CONF_PID])
    await cg.register_component(var, config)
    cg.add(var.set_status_interval(config[CONF_STATUS_INTERVAL]))
    cg.add(var.set_settings_interval(config[CONF_SETTINGS_INTERVAL]))
    cg.add(var.set_reply_timeout(config[CONF_REPLY_TIMEOUT]))
