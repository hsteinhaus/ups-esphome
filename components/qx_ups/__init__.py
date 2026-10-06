# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import usb_host
import esphome.config_validation as cv

DEPENDENCIES = ["usb_host"]
AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor"]
CODEOWNERS = ["@hsteinhaus"]

# Cypress-style HID-to-serial bridge. The id identifies the bridge, not the
# UPS behind it -- several unrelated UPS families ship the same chip.
CYPRESS_VID = 0x0665
CYPRESS_PID = 0x5161

CONF_QX_UPS_ID = "qx_ups_id"
CONF_STATUS_INTERVAL = "status_interval"
CONF_REPLY_TIMEOUT = "reply_timeout"
CONF_NOMINAL_POWER = "nominal_power"
CONF_BATTERY_VOLTAGE_LOW = "battery_voltage_low"
CONF_BATTERY_VOLTAGE_HIGH = "battery_voltage_high"

qx_ups_ns = cg.esphome_ns.namespace("qx_ups")
QxUPSClient = qx_ups_ns.class_("QxUPSClient", usb_host.USBClient)
Metric = qx_ups_ns.enum("Metric")
Flag = qx_ups_ns.enum("Flag")
Text = qx_ups_ns.enum("Text")

BASE_SCHEMA = cv.Schema({cv.GenerateID(CONF_QX_UPS_ID): cv.use_id(QxUPSClient)})


def _battery_range_complete(config):
    """A charge estimate needs both ends of the range or neither."""
    low = CONF_BATTERY_VOLTAGE_LOW in config
    high = CONF_BATTERY_VOLTAGE_HIGH in config
    if low != high:
        raise cv.Invalid(
            f"{CONF_BATTERY_VOLTAGE_LOW} and {CONF_BATTERY_VOLTAGE_HIGH} must be "
            "given together; without both, the charge estimate is derived from "
            "the voltage the UPS reports as its rating"
        )
    if low and config[CONF_BATTERY_VOLTAGE_LOW] >= config[CONF_BATTERY_VOLTAGE_HIGH]:
        raise cv.Invalid(
            f"{CONF_BATTERY_VOLTAGE_LOW} must be below {CONF_BATTERY_VOLTAGE_HIGH}"
        )
    return config


CONFIG_SCHEMA = cv.All(
    usb_host.usb_device_schema(QxUPSClient, vid=CYPRESS_VID, pid=CYPRESS_PID).extend(
        {
            # The protocol has no push: this is the detection latency, and the
            # only thing standing between a mains loss and noticing it.
            cv.Optional(
                CONF_STATUS_INTERVAL, default="1s"
            ): cv.positive_time_period_milliseconds,
            # Measured round trips on this bridge are 80-400ms. A reply that
            # misses the window is dropped rather than read as the next
            # command's answer, so this trades latency for nothing but retries.
            cv.Optional(
                CONF_REPLY_TIMEOUT, default="2s"
            ): cv.positive_time_period_milliseconds,
            # Megatec reports load as a percentage only. Watts need the plate.
            cv.Optional(CONF_NOMINAL_POWER): cv.positive_float,
            cv.Optional(CONF_BATTERY_VOLTAGE_LOW): cv.positive_float,
            cv.Optional(CONF_BATTERY_VOLTAGE_HIGH): cv.positive_float,
        }
    ),
    _battery_range_complete,
)


async def to_code(config):
    var = await usb_host.register_usb_client(config)
    cg.add(var.set_status_interval(config[CONF_STATUS_INTERVAL]))
    cg.add(var.set_reply_timeout(config[CONF_REPLY_TIMEOUT]))
    if CONF_NOMINAL_POWER in config:
        cg.add(var.set_nominal_power(config[CONF_NOMINAL_POWER]))
    if CONF_BATTERY_VOLTAGE_LOW in config:
        cg.add(
            var.set_battery_voltage_range(
                config[CONF_BATTERY_VOLTAGE_LOW], config[CONF_BATTERY_VOLTAGE_HIGH]
            )
        )
