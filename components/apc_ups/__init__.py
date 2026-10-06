# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import usb_host
import esphome.config_validation as cv

DEPENDENCIES = ["usb_host"]
AUTO_LOAD = ["sensor", "binary_sensor"]
CODEOWNERS = ["@hsteinhaus"]

APC_VID = 0x051D  # American Power Conversion
BX950MI_PID = 0x0002

CONF_APC_UPS_ID = "apc_ups_id"
CONF_POLL_INTERVAL = "poll_interval"
CONF_STATUS_INTERVAL = "status_interval"

apc_ups_ns = cg.esphome_ns.namespace("apc_ups")
APCUPSClient = apc_ups_ns.class_("APCUPSClient", usb_host.USBClient)
Metric = apc_ups_ns.enum("Metric")
Flag = apc_ups_ns.enum("Flag")

BASE_SCHEMA = cv.Schema({cv.GenerateID(CONF_APC_UPS_ID): cv.use_id(APCUPSClient)})

CONFIG_SCHEMA = usb_host.usb_device_schema(
    APCUPSClient, vid=APC_VID, pid=BX950MI_PID
).extend(
    {
        # Measurements move slowly and cost a transfer each.
        cv.Optional(
            CONF_POLL_INTERVAL, default="10s"
        ): cv.positive_time_period_milliseconds,
        # The interrupt endpoint pushes status changes, but a missed push must
        # not go unnoticed for a whole measurement period: this is the upper
        # bound on how late a mains loss can be seen.
        cv.Optional(
            CONF_STATUS_INTERVAL, default="1s"
        ): cv.positive_time_period_milliseconds,
    }
)


async def to_code(config):
    var = await usb_host.register_usb_client(config)
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
    cg.add(var.set_status_interval(config[CONF_STATUS_INTERVAL]))
