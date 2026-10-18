# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import number, uart, usb_uart
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["usb_uart"]
AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor", "number", "switch"]
CODEOWNERS = ["@hsteinhaus"]

CONF_OWON_PSU_ID = "owon_psu_id"
CONF_USB_UART_ID = "usb_uart_id"
CONF_STATUS_INTERVAL = "status_interval"
CONF_SETTINGS_INTERVAL = "settings_interval"
CONF_REPLY_TIMEOUT = "reply_timeout"
CONF_LINK_LOSS_TIMEOUT = "link_loss_timeout"
CONF_VOLTAGE_CAP = "voltage_cap"
CONF_CURRENT_CAP = "current_cap"

owon_psu_ns = cg.esphome_ns.namespace("owon_psu")
OwonPSU = owon_psu_ns.class_("OwonPSU", cg.Component, uart.UARTDevice)
Metric = owon_psu_ns.enum("Metric")
Flag = owon_psu_ns.enum("Flag")
Text = owon_psu_ns.enum("Text")
Setting = owon_psu_ns.enum("Setting")

BASE_SCHEMA = cv.Schema({cv.GenerateID(CONF_OWON_PSU_ID): cv.use_id(OwonPSU)})

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OwonPSU),
        # A usb_uart channel, not any UART: suspend_usb() has to reach the
        # USB pipe underneath.
        cv.Required(CONF_USB_UART_ID): cv.use_id(usb_uart.USBUartChannel),
        # MEAS:ALL:INFO? answers in ~55 ms, so this is far from the floor.
        cv.Optional(
            CONF_STATUS_INTERVAL, default="500ms"
        ): cv.positive_time_period_milliseconds,
        # Setpoints, limits and output only change from the front panel when
        # not changed from here; a set is read back immediately regardless.
        cv.Optional(
            CONF_SETTINGS_INTERVAL, default="5s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_REPLY_TIMEOUT, default="500ms"
        ): cv.positive_time_period_milliseconds,
        # Long enough to ride out a Wi-Fi roam or a Home Assistant restart
        # would be minutes; this errs towards switching off.
        cv.Optional(
            CONF_LINK_LOSS_TIMEOUT, default="10s"
        ): cv.positive_time_period_milliseconds,
        # Numbers, normally restoring template ones, that bound what may be
        # set from here. The front panel is not bound by them.
        cv.Optional(CONF_VOLTAGE_CAP): cv.use_id(number.Number),
        cv.Optional(CONF_CURRENT_CAP): cv.use_id(number.Number),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_usb_channel(await cg.get_variable(config[CONF_USB_UART_ID])))
    cg.add(var.set_status_interval(config[CONF_STATUS_INTERVAL]))
    cg.add(var.set_settings_interval(config[CONF_SETTINGS_INTERVAL]))
    cg.add(var.set_reply_timeout(config[CONF_REPLY_TIMEOUT]))
    cg.add(var.set_link_loss_timeout(config[CONF_LINK_LOSS_TIMEOUT]))
    if CONF_VOLTAGE_CAP in config:
        cg.add(var.set_voltage_cap(await cg.get_variable(config[CONF_VOLTAGE_CAP])))
    if CONF_CURRENT_CAP in config:
        cg.add(var.set_current_cap(await cg.get_variable(config[CONF_CURRENT_CAP])))
